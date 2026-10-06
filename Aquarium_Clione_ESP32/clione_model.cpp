#pragma GCC optimize("O3")
#include "clione_model.h"
#include <math.h>

namespace clione {
static float clamp(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static float random(World& w, float lo, float hi) {
  uint32_t x = w.rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  w.rng = x;
  return lo + (hi - lo) * (float)(x >> 8) / 16777216.0f;
}
void mouth(const World& w, const Animal& a, float& x, float& y) {
  x = a.x - sinf(a.angle) * a.size * MOUTH_Y / w.aspect;
  y = a.y + cosf(a.angle) * a.size * MOUTH_Y;
}
static int freePrey(const World& w) {
  for (int i = 0; i < PREY_COUNT; ++i) if (!w.prey[i].active) return i;
  return -1;
}
static bool encounter(World& w, int animal) {
  Animal& a = w.animals[animal];
  if (a.stage != SWIM) return false;
  const int index = freePrey(w);
  if (index < 0) return false;
  float mx, my; mouth(w, a, mx, my);
  Prey& p = w.prey[index];
  p = { clamp(mx + random(w, -.035f, .035f), .07f, .93f),
        clamp(my - .075f, .07f, .85f), 0, .003f, 12, a.size*.060f, random(w, 0, 6.2831853f),
        true, false, animal };
  a.prey = index; a.stage = APPROACH; a.age = 0;
  return true;
}
void init(World& w, uint32_t seed, float aspect) {
  w = {}; w.rng = seed ? seed : 0x434c494fu;
  w.aspect = aspect > 0 ? aspect : 4.0f / 3.0f;
  w.automatic = true;
  w.count = 1 + (int)random(w, 0, MAX_COUNT);
  if (w.count > MAX_COUNT) w.count = MAX_COUNT;
  for (int i = 0; i < w.count; ++i) {
    Animal& a = w.animals[i];
    // Scatter spawn positions; retry until the newcomer keeps its distance
    // from the animals already placed so they never appear in a row.
    for (int attempt = 0; ; ++attempt) {
      a.x = random(w, .16f, .84f);
      a.y = random(w, .30f, .72f);
      bool clear = true;
      for (int j = 0; j < i; ++j) {
        float dx = (a.x - w.animals[j].x) * w.aspect, dy = a.y - w.animals[j].y;
        if (dx * dx + dy * dy < .04f) { clear = false; break; }
      }
      if (clear || attempt >= 39) break;
    }
    // Sizes stay within the previous 0.174..0.144 range regardless of count.
    a.size = w.count > 1 ? .174f - .030f * i / (w.count - 1) : .174f; a.angle = 0;
    a.phase = i * 2.1f; a.tx = a.x; a.ty = a.y;
    a.baseRate = random(w, 1.05f, 1.4f); a.rate = a.baseRate; a.effort = .7f;
    a.driftTimer = .5f + i;
    a.yaw = -.95f+i*1.5f; a.yawTarget=a.yaw;
    a.timer = 5 + i * 7; a.age = 0; a.stage = SWIM; a.prey = -1;
  }
  w.nextFeedIn=random(w,43200,86400);
}
bool feed(World& w) {
  for (int i = 0; i < w.count; ++i) {
    int index=(w.nextFeeder+i)%w.count;
    if(encounter(w,index)){w.nextFeeder=(index+1)%w.count;w.nextFeedIn=random(w,43200,86400);return true;}
  }
  return false;
}
static void transition(Animal& a, Stage stage) { a.stage = stage; a.age = 0; }
static constexpr float STRIKE_TIME = .07f, RETRACT_TIME = 1.2f;
// Forces are in screen-height units. Position follows thrust and water drag;
// wings and translation share one oscillator rather than separate animations.
Stroke wingStroke(const Animal& a) {
  // Slow closed passage (12%), open (38%), close (50%).
  // A 10% continuous phase drift avoids stopping the membrane dead.
  constexpr float tau=6.283185307f,closed=3.70f,opened=6.80f;
  float clock=a.phase/tau;
  if(clock<.12f)return {closed+.10f*tau*clock,.10f};
  float duration,start,travel,t;
  if(clock<.50f){duration=.38f;start=closed;travel=opened-closed;t=(clock-.12f)/duration;}
  else {duration=.50f;start=opened;travel=closed+tau-opened;t=(clock-.50f)/duration;}
  t=clamp(t,0,1);
  return {.90f*(start+travel*t*t*(3-2*t))+.10f*(closed+tau*clock),
          .90f*travel*6*t*(1-t)/(duration*tau)+.10f};
}
static void swimMotion(World& w, Animal& a, int index, float dt) {
  const bool hunting = a.stage == APPROACH;
  const bool holding = a.stage >= STRIKE && a.stage <= RETRACT;
  if (a.stage == SWIM || a.stage == RELEASE) {
    a.driftTimer -= dt;
    if (a.driftTimer <= 0) {
      a.tx = clamp(a.x + random(w, -.26f, .26f), .18f, .82f);
      a.ty = random(w, .33f, .74f);
      a.driftTimer = random(w, 6, 14);
      a.yawTarget=atan2f((a.tx-a.x)*w.aspect,random(w,-.22f,.22f));
    }
  }
  if(hunting) a.yawTarget=a.tx<a.x?-.28f:.28f;
  // Shortest arc, damped turn: never flip a sprite in one frame.
  float yawError=atan2f(sinf(a.yawTarget-a.yaw),cosf(a.yawTarget-a.yaw));
  a.yawVelocity+=(yawError*(hunting?4.5f:2)-a.yawVelocity*3)*dt;
  a.yawVelocity=clamp(a.yawVelocity,-.8f,.8f);
  a.yaw+=a.yawVelocity*dt;
  a.yaw=atan2f(sinf(a.yaw),cosf(a.yaw));
  float heading = holding ? 0 : clamp((a.tx-a.x)*3.8f, -.62f, .62f);
  if (!hunting && !holding) {
    // A gentle avoidance turn, not a bounce or a fixed lane per animal.
    for (int j=0; j<w.count; ++j) if (j!=index) {
      float dx=(a.x-w.animals[j].x)*w.aspect, dy=a.y-w.animals[j].y;
      float d2=dx*dx+dy*dy;
      if(d2<.0256f) heading += (dx<0?-.25f:.25f)*(1-d2/.0256f);
    }
  }
  heading=clamp(heading,-.7f,.7f);
  a.heading += (heading-a.heading)*dt*1.3f;
  a.turn += ((a.heading-a.angle)*9-a.turn*6)*dt;
  a.angle += a.turn*dt;
  float rate=a.baseRate*(hunting?1.65f:(holding?.72f:1));
  a.rate += (rate-a.rate)*dt*2.5f;
  a.phase = fmodf(a.phase+dt*a.rate*6.283185307f,6.283185307f);
  float desiredY=holding?0:clamp((a.ty-a.y)*.22f,-.035f,.025f);
  float effort=clamp((.09f-desiredY*1.8f)/(.13f*cosf(a.angle)),.28f,1.15f);
  a.effort += (effort-a.effort)*dt*2;
  const Stroke stroke=wingStroke(a);
  const float thrust=.13f*a.effort*(1+.65f*cosf(stroke.phase*2))*stroke.speed;
  const float current=.003f*sinf(w.time*.21f+index*1.7f);
  const float active=clamp(stroke.speed,0,1),drag=.6f+1.2f*active;
  // Closed wings coast with retained velocity; no downward acceleration.
  a.vx += (sinf(a.angle)*thrust-a.vx*drag+current*active)*dt;
  a.vy += (.09f*stroke.speed-cosf(a.angle)*thrust-a.vy*drag)*dt;
  if (hunting) {
    // Small steering correction while the faster wingbeat supplies motion.
    a.vx += ((a.tx-a.x)*w.aspect*1.8f-a.vx)*dt*3;
    a.vy += ((a.ty-a.y)*1.8f-a.vy)*dt*3;
  }
  if(a.x<.18f)a.vx+=(.18f-a.x)*dt;
  if(a.x>.82f)a.vx+=(.82f-a.x)*dt;
  if(a.y<.28f)a.vy+=(.28f-a.y)*dt;
  if(a.y>.77f)a.vy+=(.77f-a.y)*dt;
  a.x += a.vx*dt/w.aspect; a.y += a.vy*dt;
  // Emergency guard only; soft forces turn the animal before these limits.
  a.x=clamp(a.x,.12f,.88f); a.y=clamp(a.y,.23f,.82f);
}
static void tick(World& w, float dt) {
  w.time += dt;
  if(w.automatic){w.nextFeedIn-=dt;if(w.nextFeedIn<=0&&!feed(w))w.nextFeedIn=1;}
  for (int i = 0; i < PREY_COUNT; ++i) {
    Prey& p = w.prey[i];
    if (!p.active || p.owner >= 0) continue;
    p.life -= dt; p.x += p.vx * dt; p.y += p.vy * dt;
    if (p.life <= 0 || p.y > .93f || p.y < .04f || p.x < .02f || p.x > .98f) p.active = false;
  }
  for (int i = 0; i < w.count; ++i) {
    Animal& a = w.animals[i];
    if (a.stage == APPROACH && a.prey >= 0 && w.prey[a.prey].active) {
      a.tx = w.prey[a.prey].x;
      a.ty = w.prey[a.prey].y - a.size*MOUTH_Y + a.size*.26f;
    }
    swimMotion(w, a, i, dt);
    a.age += dt;
    if (a.stage == SWIM) {

      continue;
    }
    if (a.prey < 0 || a.prey >= PREY_COUNT || !w.prey[a.prey].active) {
      a.prey = -1; a.timer = 4; transition(a, SWIM); continue;
    }
    Prey& p = w.prey[a.prey];
    float mx, my; mouth(w, a, mx, my);
    if (a.stage == APPROACH) {
      if (a.age >= 1.2f) {
        a.vx += sinf(a.angle)*.12f; a.vy -= cosf(a.angle)*.12f;
        transition(a, STRIKE);
      }
    } else if (a.stage == STRIKE) {
      // Cones inflate in 50-70 ms and close over the shell.
      p.x += (mx + sinf(a.angle)*a.size*.14f/w.aspect - p.x) * dt * 30;
      p.y += (my - cosf(a.angle)*a.size*.14f - p.y) * dt * 30;
      if (a.age >= STRIKE_TIME) {
        ++w.captures; a.timer = random(w, 15, 30); transition(a, HOLD);
      }
    } else if (a.stage == HOLD || a.stage == EXTRACT || a.stage == RETRACT) {
      const float reach = a.stage==HOLD ? .14f-.06f*grasp(a) : .08f;
      p.x += (mx + sinf(a.angle)*a.size*reach/w.aspect - p.x)*dt*22;
      p.y += (my - cosf(a.angle)*a.size*reach - p.y)*dt*22;
      if (a.stage == HOLD) {
        // The cones turn the shell until its aperture faces the mouth.
        const float target = atan2f(my - p.y, (mx - p.x)*w.aspect) - APERTURE_ANGLE;
        const float error = atan2f(sinf(target - p.spin), cosf(target - p.spin));
        p.spin += clamp(error, -.35f*dt, .35f*dt);
      }
      // Pulling the body out with the hooks takes minutes, not seconds.
      if (a.stage == HOLD && a.age >= a.timer) { a.timer = random(w, 600, 1800); transition(a, EXTRACT); }
      else if (a.stage == EXTRACT && a.age >= a.timer) { p.empty = true; transition(a, RETRACT); }
      else if (a.stage == RETRACT && a.age >= RETRACT_TIME) {
        p.owner = -1; p.life = 7; p.vx = .01f; p.vy = .025f;
        ++w.releases; transition(a, RELEASE);
      }
    } else if (a.stage == RELEASE && a.age >= .8f) {
      a.prey = -1; a.timer = random(w, 24, 48); a.driftTimer = 0; transition(a, SWIM);
    }
    a.x = clamp(a.x, .12f, .88f); a.y = clamp(a.y, .23f, .8f);
  }
}
void step(World& w, float dt) {
  // Short strike plus a readable open/curl phase, independent of panel frame rate.
  if (!(dt > 0) || !isfinite(dt)) return;
  dt = fminf(dt, .25f);
  while (dt > .000001f) { float part = fminf(dt, 1.0f / 120); tick(w, part); dt -= part; }
}
float extension(const Animal& a) {
  if (a.stage == STRIKE) return clamp(a.age / STRIKE_TIME, 0, 1);
  if (a.stage == HOLD || a.stage == EXTRACT) return 1;
  if (a.stage == RETRACT) return clamp(1 - a.age / RETRACT_TIME, 0, 1);
  return 0;
}
float grasp(const Animal& a) {
  if (a.stage==HOLD) {
    float t=clamp(a.age/.35f,0,1);return t*t*(3-2*t);
  }
  if (a.stage==EXTRACT || a.stage==RETRACT) return 1;
  return 0;
}
int frame(const Animal&) {
  // A single stable body texture. The renderer articulates both wings and tail.
  return 2;
}
const char* stageName(Stage s) {
  static const char* names[] = {"swim","approach","strike","hold","extract","retract","release"};
  return (unsigned)s <= RELEASE ? names[s] : "invalid";
}
}
