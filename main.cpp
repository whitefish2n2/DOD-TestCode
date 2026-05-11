#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <random>
#include <algorithm>

const int MAX_OBJECTS = 50000;
const int ITERATIONS = 5000;
const int CHURN_PER_FRAME = 1000;

// ==========================================
// 1. 순수 OOP 방식 (포인터 추적 + 동적 바인딩)
// ==========================================
class TransformComponentOOP {
public:
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int dummy[4] = {0};
};

class ComponentOOP {
public:
    virtual ~ComponentOOP() = default;
    virtual void update() = 0;
};

class PhysicsComponentOOP : public ComponentOOP {
public:
    float vx = 0.1f, vy = 0.1f, vz = 0.1f;
    TransformComponentOOP* targetTransform;
    int dummy[3] = {0};

    PhysicsComponentOOP(TransformComponentOOP* target) : targetTransform(target) {}

    void update() override {
        if (targetTransform) {
            targetTransform->x += vx;
            targetTransform->y += vy;
            targetTransform->z += vz;
        }
    }
};

class GameObject {
public:
    TransformComponentOOP* trans;
    ComponentOOP* phys;

    GameObject() {
        trans = new TransformComponentOOP();
        phys = new PhysicsComponentOOP(trans);
    }
    ~GameObject() {
        delete phys;
        delete trans;
    }
    void update() {
        if (phys) phys->update(); // vTable을 통한 동적 호출
    }
};

// ==========================================
// 2. Hybrid DOD 방식 (상속 유지 + 타입 배열 + 탈가상화)
// ==========================================
struct TransformComponentDOD {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    int dummy[4] = {0};
};

// DOD용 Base 컴포넌트 (상속 구조 유지)
class ComponentDOD {
public:
    virtual ~ComponentDOD() = default;
    virtual void update(std::vector<TransformComponentDOD>& transforms) = 0;
};

// Base를 상속받은 물리 컴포넌트
class PhysicsComponentDOD : public ComponentDOD {
public:
    float vx = 0.1f, vy = 0.1f, vz = 0.1f;
    int targetIndex;
    int dummy[3] = {0};

    // 'final' 키워드로 탈가상화(Devirtualization)와 인라인 최적화를 강력히 유도
    void update(std::vector<TransformComponentDOD>& transforms) final {
        transforms[targetIndex].x += vx;
        transforms[targetIndex].y += vy;
        transforms[targetIndex].z += vz;
    }
};

// ==========================================
// 벤치마크 실행 함수
// ==========================================
double run_real_oop_benchmark(float& outDummyResult) {
    std::vector<GameObject*> gameObjects;
    for (int i = 0; i < MAX_OBJECTS; ++i) {
        gameObjects.push_back(new GameObject());
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        for (auto go : gameObjects) {
            go->update(); // 더블 포인터 체이싱 + vTable 오버헤드
        }

        for (int j = 0; j < CHURN_PER_FRAME; ++j) {
            if (!gameObjects.empty()) {
                int idx = rand() % gameObjects.size();
                delete gameObjects[idx];
                std::swap(gameObjects[idx], gameObjects.back());
                gameObjects.pop_back();
            }
            gameObjects.push_back(new GameObject());
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    if (!gameObjects.empty()) outDummyResult += gameObjects.back()->trans->x;
    for (auto go : gameObjects) delete go;
    return std::chrono::duration<double, std::milli>(end - start).count();
}

double run_dod_benchmark(float& outDummyResult) {
    std::vector<TransformComponentDOD> transforms(MAX_OBJECTS);

    // [핵심] Base 포인터 배열이 아닌, 파생 클래스(PhysicsComponentDOD)의 구체 타입 값 배열
    std::vector<PhysicsComponentDOD> physics(MAX_OBJECTS);

    for(int i = 0; i < MAX_OBJECTS; ++i) {
        physics[i].targetIndex = i;
    }

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < ITERATIONS; ++i) {
        // 컴파일러는 여기서 'physics' 배열 요소가 정확히 PhysicsComponentDOD 임을 앎
        // vTable 무시하고 직접 함수 호출 (Devirtualization 발생)
        for (auto& p : physics) {
            p.update(transforms);
        }

        for (int j = 0; j < CHURN_PER_FRAME; ++j) {
            if (!physics.empty()) {
                int idx = rand() % physics.size();
                std::swap(physics[idx], physics.back());
                physics.pop_back();
            }
            physics.push_back(PhysicsComponentDOD{});
            physics.back().targetIndex = rand() % MAX_OBJECTS;
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    outDummyResult += transforms[0].x;
    return std::chrono::duration<double, std::milli>(end - start).count();
}

int main() {
    float dummyOut = 0.0f;
    int TEST_COUNT = 3;

    std::cout << "교차 실행 횟수 입력: ";
    std::cin >> TEST_COUNT;

    std::vector<double> oopResults, dodResults;

    for (int i = 1; i <= TEST_COUNT; ++i) {
        std::cout << "\n[" << i << "회차 테스트 중...]\n";

        srand(1234125);
        double tOOP = run_real_oop_benchmark(dummyOut);
        oopResults.push_back(tOOP);
        std::cout << "  - OOP (Pointer Chasing + vTable) : " << tOOP << " ms\n";

        srand(1234125);
        double tDOD = run_dod_benchmark(dummyOut);
        dodResults.push_back(tDOD);
        std::cout << "  - DOD (Devirtualization + Array) : " << tDOD << " ms\n";
    }

    double avgOOP = std::accumulate(oopResults.begin(), oopResults.end(), 0.0) / TEST_COUNT;
    double avgDOD = std::accumulate(dodResults.begin(), dodResults.end(), 0.0) / TEST_COUNT;

    std::cout << "\n========================================================\n";
    std::cout << "최종 결과 (포인터+가상함수 vs 구체 타입 배열+탈가상화)\n";
    std::cout << "1. 순수 OOP (GameObject -> Component* -> Transform*) 평균 : " << avgOOP << " ms\n";
    std::cout << "2. Hybrid DOD (std::vector<PhysicsComponentDOD> 상속) 평균: " << avgDOD << " ms\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "=> Hybrid DOD가 약 " << avgOOP / avgDOD << "배 더 빠릅니다.\n";
    std::cout << "========================================================\n";

    return 0;
}