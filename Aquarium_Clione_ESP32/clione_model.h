#pragma once
#include <stdint.h>

namespace clione {
constexpr int MAX_COUNT = 5;  // 1..MAX_COUNT animals, drawn at init
constexpr int PREY_COUNT = 8;
constexpr float MOUTH_Y = -0.35f;  // normalized sprite coordinate from its centre
constexpr float APERTURE_ANGLE = 0.709f;  // shell aperture direction at spin 0
enum Stage : uint8_t { SWIM, APPROACH, STRIKE, HOLD, EXTRACT, RETRACT, RELEASE };
struct Animal {
  float x, y, size, angle, phase, tx, ty, timer, age;
  float vx, vy, turn, heading, rate, baseRate, effort, driftTimer;
  float yaw, yawTarget, yawVelocity;
  Stage stage;
  int prey;
};
struct Prey {
  float x, y, vx, vy, life, size, spin;  // spin: shell orientation (rad)
  bool active, empty;
  int owner;
};
struct World {
  Animal animals[MAX_COUNT];
  int count;
  Prey prey[PREY_COUNT];
  uint32_t rng;
  float time, aspect;
  bool automatic;
  double nextFeedIn;
  int nextFeeder;
  uint32_t captures, releases;
};
void init(World& world, uint32_t seed, float aspect);
void step(World& world, float dt);
bool feed(World& world); // adds one prey encounter; never interrupts a capture
void mouth(const World& world, const Animal& animal, float& x, float& y);
struct Stroke { float phase, speed; };
Stroke wingStroke(const Animal& animal);
float extension(const Animal& animal);
float grasp(const Animal& animal);
int frame(const Animal& animal);
const char* stageName(Stage stage);
}
