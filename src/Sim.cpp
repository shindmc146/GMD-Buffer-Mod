#include "Sim.hpp"
#include "Logic.hpp"

#include <Geode/modify/EnhancedGameObject.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/GameObject.hpp>
#include <Geode/modify/HardStreak.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <vector>

using namespace geode::prelude;

namespace {

PlayerObject* g_ghosts[2] = {nullptr, nullptr};
bool g_simulating = false;
bool g_died = false;
bool g_stopped = false;

float g_accumDt = 0.f;
float g_stepDt = 0.f;
bm::logic::StableValue g_tps(240, 30);
uint64_t g_totalSteps = 0;

bool ghostPtr(PlayerObject* p) {
    return p && (p == g_ghosts[0] || p == g_ghosts[1]);
}

// Object types the ghost collides with normally.
bool physicalType(GameObjectType t) {
    switch (t) {
        case GameObjectType::Solid:
        case GameObjectType::Hazard:
        case GameObjectType::Slope:
        case GameObjectType::AnimatedHazard:
        case GameObjectType::Breakable:
            return true;
        default:
            return false;
    }
}

// Objects that change the ghost's path in ways the lookahead can't follow.
// They stay enabled so the ghost "notices" them, and the simulation stops there.
bool interactiveType(GameObjectType t) {
    switch (t) {
        case GameObjectType::InverseGravityPortal:
        case GameObjectType::NormalGravityPortal:
        case GameObjectType::ShipPortal:
        case GameObjectType::CubePortal:
        case GameObjectType::YellowJumpPad:
        case GameObjectType::PinkJumpPad:
        case GameObjectType::GravityPad:
        case GameObjectType::YellowJumpRing:
        case GameObjectType::PinkJumpRing:
        case GameObjectType::GravityRing:
        case GameObjectType::InverseMirrorPortal:
        case GameObjectType::NormalMirrorPortal:
        case GameObjectType::BallPortal:
        case GameObjectType::UfoPortal:
        case GameObjectType::DualPortal:
        case GameObjectType::SoloPortal:
        case GameObjectType::WavePortal:
        case GameObjectType::RobotPortal:
        case GameObjectType::TeleportPortal:
        case GameObjectType::GreenRing:
        case GameObjectType::DropRing:
        case GameObjectType::SpiderPortal:
        case GameObjectType::RedJumpPad:
        case GameObjectType::RedJumpRing:
        case GameObjectType::CustomRing:
        case GameObjectType::DashRing:
        case GameObjectType::GravityDashRing:
        case GameObjectType::SwingPortal:
        case GameObjectType::GravityTogglePortal:
        case GameObjectType::SpiderOrb:
        case GameObjectType::SpiderPad:
        case GameObjectType::TeleportOrb:
            return true;
        default:
            return false;
    }
}

// Speed and size portals are simple enough to apply to the ghost directly.
bool applySimplePortal(PlayerObject* p, int id) {
    switch (id) {
        case 200: p->m_playerSpeed = 0.7f; return true;
        case 201: p->m_playerSpeed = 0.9f; return true;
        case 202: p->m_playerSpeed = 1.1f; return true;
        case 203: p->m_playerSpeed = 1.3f; return true;
        case 1334: p->m_playerSpeed = 1.6f; return true;
        case 101:
            if (p->m_vehicleSize == 1.0f) p->togglePlayerScale(true, true);
            return true;
        case 99:
            if (p->m_vehicleSize != 1.0f) p->togglePlayerScale(false, true);
            return true;
        default:
            return false;
    }
}

bool simplePortalId(int id) {
    return id == 200 || id == 201 || id == 202 || id == 203 || id == 1334 || id == 101 || id == 99;
}

// Coins, keys and other pickups never reach the ghost.
bool collectibleId(int id) {
    return id == 1329 || id == 1275 || id == 1587 || id == 1589 || id == 1598 || id == 1614 ||
           id == 3601 || (id >= 4401 && id <= 4539);
}

void clearCollisionLog(PlayerObject* p) {
    if (p->m_collisionLogTop) p->m_collisionLogTop->removeAllObjects();
    if (p->m_collisionLogBottom) p->m_collisionLogBottom->removeAllObjects();
    if (p->m_collisionLogLeft) p->m_collisionLogLeft->removeAllObjects();
    if (p->m_collisionLogRight) p->m_collisionLogRight->removeAllObjects();
    p->m_lastCollisionTop = -1;
    p->m_lastCollisionBottom = -1;
    p->m_lastCollisionLeft = -1;
    p->m_lastCollisionRight = -1;
}

// Switches the ghost to the real icon's game mode and size through the
// game's own toggles, so every sprite the mode needs exists.
void syncMode(PlayerObject* g, PlayerObject* p) {
    bool same = g->m_isShip == p->m_isShip && g->m_isBird == p->m_isBird && g->m_isBall == p->m_isBall &&
                g->m_isDart == p->m_isDart && g->m_isRobot == p->m_isRobot && g->m_isSpider == p->m_isSpider &&
                g->m_isSwing == p->m_isSwing;
    if (!same) {
        if (g->m_isShip) g->toggleFlyMode(false, true);
        if (g->m_isBird) g->toggleBirdMode(false, true);
        if (g->m_isBall) g->toggleRollMode(false, true);
        if (g->m_isDart) g->toggleDartMode(false, true);
        if (g->m_isRobot) g->toggleRobotMode(false, true);
        if (g->m_isSpider) g->toggleSpiderMode(false, true);
        if (g->m_isSwing) g->toggleSwingMode(false, true);
        if (p->m_isShip) g->toggleFlyMode(true, true);
        if (p->m_isBird) g->toggleBirdMode(true, true);
        if (p->m_isBall) g->toggleRollMode(true, true);
        if (p->m_isDart) g->toggleDartMode(true, true);
        if (p->m_isRobot) g->toggleRobotMode(true, true);
        if (p->m_isSpider) g->toggleSpiderMode(true, true);
        if (p->m_isSwing) g->toggleSwingMode(true, true);
    }
    bool mini = p->m_vehicleSize != 1.0f;
    if ((g->m_vehicleSize != 1.0f) != mini) g->togglePlayerScale(mini, true);
}

// Copies everything the physics step reads from the real icon to the ghost.
void copyState(PlayerObject* g, PlayerObject* p) {
    syncMode(g, p);

    // game mode and size
    g->m_isShip = p->m_isShip;
    g->m_isBird = p->m_isBird;
    g->m_isBall = p->m_isBall;
    g->m_isDart = p->m_isDart;
    g->m_isRobot = p->m_isRobot;
    g->m_isSpider = p->m_isSpider;
    g->m_isSwing = p->m_isSwing;
    g->m_vehicleSize = p->m_vehicleSize;
    g->setScaleX(p->getScaleX());
    g->setScaleY(p->getScaleY());

    // position, rotation, motion
    g->setPosition(p->getPosition());
    g->m_position = p->m_position;
    g->m_lastPosition = p->m_lastPosition;
    g->setRotation(p->getRotation());
    g->m_yVelocity = p->m_yVelocity;
    g->m_fallSpeed = p->m_fallSpeed;
    g->m_gravity = p->m_gravity;
    g->m_gravityMod = p->m_gravityMod;
    g->m_playerSpeed = p->m_playerSpeed;
    g->m_playerSpeedAC = p->m_playerSpeedAC;
    g->m_speedMultiplier = p->m_speedMultiplier;
    g->m_accelerationOrSpeed = p->m_accelerationOrSpeed;
    g->m_platformerXVelocity = p->m_platformerXVelocity;
    g->m_yVelocityBeforeSlope = p->m_yVelocityBeforeSlope;
    g->m_rotationSpeed = p->m_rotationSpeed;
    g->m_rotateSpeed = p->m_rotateSpeed;
    g->m_isRotating = p->m_isRotating;
    g->m_isBallRotating = p->m_isBallRotating;
    g->m_isBallRotating2 = p->m_isBallRotating2;

    // orientation and flags
    g->m_isUpsideDown = p->m_isUpsideDown;
    g->m_isGoingLeft = p->m_isGoingLeft;
    g->m_isSideways = p->m_isSideways;
    g->m_isPlatformer = p->m_isPlatformer;
    g->m_isSecondPlayer = p->m_isSecondPlayer;
    g->m_isLocked = p->m_isLocked;
    g->m_controlsDisabled = p->m_controlsDisabled;
    g->m_inputsLocked = p->m_inputsLocked;
    g->m_fixGravityBug = p->m_fixGravityBug;
    g->m_fixRobotJump = p->m_fixRobotJump;
    g->m_enable22Changes = p->m_enable22Changes;
    g->m_reverseRelated = p->m_reverseRelated;
    g->m_maybeReverseSpeed = p->m_maybeReverseSpeed;
    g->m_maybeReverseAcceleration = p->m_maybeReverseAcceleration;
    g->m_reverseSync = p->m_reverseSync;

    // ground and collision state
    g->m_isOnGround = p->m_isOnGround;
    g->m_isOnGround2 = p->m_isOnGround2;
    g->m_isOnGround3 = p->m_isOnGround3;
    g->m_isOnGround4 = p->m_isOnGround4;
    g->m_lastGroundedPos = p->m_lastGroundedPos;
    g->m_lastLandTime = p->m_lastLandTime;
    g->m_groundYVelocity = p->m_groundYVelocity;
    g->m_groundObjectMaterial = p->m_groundObjectMaterial;
    g->m_lastGroundObject = p->m_lastGroundObject;
    g->m_maybeLastGroundObject = p->m_maybeLastGroundObject;
    g->m_preLastGroundObject = p->m_preLastGroundObject;
    g->m_collidedObject = p->m_collidedObject;
    g->m_collidingWithLeft = p->m_collidingWithLeft;
    g->m_collidingWithRight = p->m_collidingWithRight;
    g->m_collidedTopMinY = p->m_collidedTopMinY;
    g->m_collidedBottomMaxY = p->m_collidedBottomMaxY;
    g->m_collidedLeftMaxX = p->m_collidedLeftMaxX;
    g->m_collidedRightMinX = p->m_collidedRightMinX;
    g->m_maybeIsColliding = p->m_maybeIsColliding;
    g->m_maybeIsFalling = p->m_maybeIsFalling;
    g->m_objectSnappedTo = p->m_objectSnappedTo;
    g->m_snapDistance = p->m_snapDistance;
    g->m_isAccelerating = p->m_isAccelerating;
    g->m_isOnIce = p->m_isOnIce;
    g->m_isSliding = p->m_isSliding;
    g->m_isSlidingRight = p->m_isSlidingRight;
    g->m_maybeSlidingTime = p->m_maybeSlidingTime;
    g->m_maybeSlidingStartTime = p->m_maybeSlidingStartTime;
    g->m_isMoving = p->m_isMoving;
    g->m_affectedByForces = p->m_affectedByForces;
    g->m_maybeIsBoosted = p->m_maybeIsBoosted;
    g->m_decreaseBoostSlide = p->m_decreaseBoostSlide;

    // slopes
    g->m_isOnSlope = p->m_isOnSlope;
    g->m_wasOnSlope = p->m_wasOnSlope;
    g->m_isCollidingWithSlope = p->m_isCollidingWithSlope;
    g->m_collidingWithSlopeId = p->m_collidingWithSlopeId;
    g->m_isCurrentSlopeTop = p->m_isCurrentSlopeTop;
    g->m_currentSlope = p->m_currentSlope;
    g->m_currentSlope2 = p->m_currentSlope2;
    g->m_currentSlopeYVelocity = p->m_currentSlopeYVelocity;
    g->m_slopeRotation = p->m_slopeRotation;
    g->m_slopeVelocity = p->m_slopeVelocity;
    g->m_slopeAngle = p->m_slopeAngle;
    g->m_slopeAngleRadians = p->m_slopeAngleRadians;
    g->m_slopeStartTime = p->m_slopeStartTime;
    g->m_slopeEndTime = p->m_slopeEndTime;
    g->m_maybeUpsideDownSlope = p->m_maybeUpsideDownSlope;
    g->m_maybeSlopeForce = p->m_maybeSlopeForce;
    g->m_slopeFlipGravityRelated = p->m_slopeFlipGravityRelated;
    g->m_slopeSlidingMaybeRotated = p->m_slopeSlidingMaybeRotated;

    // jump, ring and hold state
    g->m_holdingButtons = p->m_holdingButtons;
    g->m_holdingLeft = p->m_holdingLeft;
    g->m_holdingRight = p->m_holdingRight;
    g->m_leftPressedFirst = p->m_leftPressedFirst;
    g->m_platformerMovingLeft = p->m_platformerMovingLeft;
    g->m_platformerMovingRight = p->m_platformerMovingRight;
    g->m_jumpBuffered = p->m_jumpBuffered;
    g->m_wasJumpBuffered = p->m_wasJumpBuffered;
    g->m_stateJumpBuffered = p->m_stateJumpBuffered;
    g->m_stateRingJump = p->m_stateRingJump;
    g->m_stateRingJump2 = p->m_stateRingJump2;
    g->m_ringJumpRelated = p->m_ringJumpRelated;
    g->m_touchedRing = p->m_touchedRing;
    g->m_touchedCustomRing = p->m_touchedCustomRing;
    g->m_touchedGravityPortal = p->m_touchedGravityPortal;
    g->m_touchedPad = p->m_touchedPad;
    g->m_padRingRelated = p->m_padRingRelated;
    g->m_touchedRings = p->m_touchedRings;
    g->m_ringRelatedSet = p->m_ringRelatedSet;
    g->m_jumpPadRelated = p->m_jumpPadRelated;
    g->m_wasRobotJump = p->m_wasRobotJump;
    g->m_wasTeleported = p->m_wasTeleported;
    g->m_hasEverJumped = p->m_hasEverJumped;
    g->m_lastJumpTime = p->m_lastJumpTime;
    g->m_lastFlipTime = p->m_lastFlipTime;
    g->m_lastSpiderFlipTime = p->m_lastSpiderFlipTime;
    g->m_blackOrbRelated = p->m_blackOrbRelated;
    g->m_lastActivatedPortal = p->m_lastActivatedPortal;
    g->m_lastPortalPos = p->m_lastPortalPos;
    g->m_jumpRelatedAC2 = p->m_jumpRelatedAC2;

    // internal state machine values
    g->m_stateOnGround = p->m_stateOnGround;
    g->m_stateNoStickX = p->m_stateNoStickX;
    g->m_stateNoStickY = p->m_stateNoStickY;
    g->m_stateUnk = p->m_stateUnk;
    g->m_stateUnk2 = p->m_stateUnk2;
    g->m_stateBoostX = p->m_stateBoostX;
    g->m_stateBoostY = p->m_stateBoostY;
    g->m_maybeStateForce2 = p->m_maybeStateForce2;
    g->m_stateScale = p->m_stateScale;
    g->m_stateNoAutoJump = p->m_stateNoAutoJump;
    g->m_stateDartSlide = p->m_stateDartSlide;
    g->m_stateHitHead = p->m_stateHitHead;
    g->m_stateFlipGravity = p->m_stateFlipGravity;
    g->m_stateForce = p->m_stateForce;
    g->m_physDeltaRelated = p->m_physDeltaRelated;
    g->m_totalTime = p->m_totalTime;
    g->m_gameModeChangedTime = p->m_gameModeChangedTime;
    g->m_yVelocityRelated = p->m_yVelocityRelated;
    g->m_yVelocityRelated3 = p->m_yVelocityRelated3;
    g->m_xVelocityRelated = p->m_xVelocityRelated;
    g->m_xVelocityRelated2 = p->m_xVelocityRelated2;
    g->m_platformerVelocityRelated = p->m_platformerVelocityRelated;
    g->m_unk669 = p->m_unk669;
    g->m_unkA29 = p->m_unkA29;
    g->m_unk3d0 = p->m_unk3d0;
    g->m_unk3e0 = p->m_unk3e0;
    g->m_unk3e1 = p->m_unk3e1;

    // never simulate these
    g->m_isDashing = false;
    g->m_isDead = false;
    g->m_isOutOfBounds = false;
    if (g->m_touchingRings) g->m_touchingRings->removeAllObjects();
    clearCollisionLog(g);
}

PlayerObject* createGhost(PlayLayer* pl) {
    auto objectLayer = pl->m_objectLayer;
    if (!objectLayer) return nullptr;
    unsigned layerBefore = pl->getChildrenCount();
    unsigned objectsBefore = objectLayer->getChildrenCount();

    auto ghost = PlayerObject::create(1, 1, pl, pl, true);
    if (!ghost) return nullptr;
    ghost->m_playEffects = false;
    ghost->setVisible(false);
    objectLayer->addChild(ghost);

    // hide anything the constructor attached around it (particles, trails)
    auto hideNew = [ghost](CCNode* parent, unsigned from) {
        auto children = parent->getChildren();
        if (!children) return;
        for (unsigned i = from; i < children->count(); i++) {
            auto node = typeinfo_cast<CCNode*>(children->objectAtIndex(i));
            if (node && node != ghost) node->setVisible(false);
        }
    };
    hideNew(pl, layerBefore);
    hideNew(objectLayer, objectsBefore);
    if (ghost->m_regularTrail) ghost->m_regularTrail->setVisible(false);
    if (ghost->m_shipStreak) ghost->m_shipStreak->setVisible(false);
    if (ghost->m_waveTrail) ghost->m_waveTrail->setVisible(false);
    return ghost;
}

} // namespace

// ---- ghost storage (lives and dies with the level) ---------------------------

class $modify(SimPlayLayer, PlayLayer) {
    struct Fields {
        Ref<PlayerObject> ghosts[2];

        ~Fields() {
            for (int i = 0; i < 2; i++) {
                if (g_ghosts[i] == ghosts[i].data()) g_ghosts[i] = nullptr;
            }
        }
    };

    bool ensureGhosts(int count) {
        auto f = m_fields.self();
        for (int i = 0; i < count && i < 2; i++) {
            if (!f->ghosts[i]) f->ghosts[i] = createGhost(this);
            g_ghosts[i] = f->ghosts[i].data();
            if (!g_ghosts[i]) return false;
        }
        return true;
    }

    void playEndAnimationToPos(CCPoint pos) {
        if (g_simulating) return;
        PlayLayer::playEndAnimationToPos(pos);
    }
};

// ---- ghost-only blockers on the player ----------------------------------------

class $modify(SimPlayerObject, PlayerObject) {
    void update(float dt) {
        if (!g_simulating && !ghostPtr(this)) {
            auto pl = PlayLayer::get();
            if (pl && this == pl->m_player1 && dt > 0.f) g_accumDt += dt;
        }
        PlayerObject::update(dt);
    }

    void ringJump(RingObject* ring, bool skipCheck) {
        if (ghostPtr(this)) {
            g_stopped = true;
            return;
        }
        PlayerObject::ringJump(ring, skipCheck);
    }

    void incrementJumps() {
        if (ghostPtr(this)) return;
        PlayerObject::incrementJumps();
    }
    void playerDestroyed(bool noEffects) {
        if (ghostPtr(this)) return;
        PlayerObject::playerDestroyed(noEffects);
    }
    void playSpawnEffect() {
        if (ghostPtr(this)) return;
        PlayerObject::playSpawnEffect();
    }
    void spawnCircle() {
        if (ghostPtr(this)) return;
        PlayerObject::spawnCircle();
    }
    void spawnDualCircle() {
        if (ghostPtr(this)) return;
        PlayerObject::spawnDualCircle();
    }
    void playSpiderDashEffect(CCPoint from, CCPoint to) {
        if (ghostPtr(this)) return;
        PlayerObject::playSpiderDashEffect(from, to);
    }
    void playDynamicSpiderRun() {
        if (ghostPtr(this)) return;
        PlayerObject::playDynamicSpiderRun();
    }
    void createFadeOutDartStreak() {
        if (ghostPtr(this)) return;
        PlayerObject::createFadeOutDartStreak();
    }
    void playBumpEffect(int type, GameObject* object) {
        if (ghostPtr(this)) return;
        PlayerObject::playBumpEffect(type, object);
    }
    void placeStreakPoint() {
        if (ghostPtr(this)) return;
        PlayerObject::placeStreakPoint();
    }
};

// ---- level-side blockers while a ghost runs ------------------------------------

class $modify(SimGameLayer, GJBaseGameLayer) {
    void collisionCheckObjects(PlayerObject* player, gd::vector<GameObject*>* objects, int objectCount, float dt) {
        if (!g_simulating || !ghostPtr(player) || !objects) {
            GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
            return;
        }
        // Turn off everything that isn't solid, deadly or interactive for this
        // check, so the ghost can't collect, trigger or toggle anything.
        static std::vector<GameObject*> disabled;
        disabled.clear();
        for (auto obj : *objects) {
            if (!obj) continue;
            int id = obj->m_objectID;
            bool keep = !collectibleId(id) &&
                        (physicalType(obj->m_objectType) || interactiveType(obj->m_objectType) || simplePortalId(id));
            if (keep || obj->m_isDisabled || obj->m_isDisabled2) continue;
            obj->m_isDisabled = true;
            obj->m_isDisabled2 = true;
            disabled.push_back(obj);
        }
        GJBaseGameLayer::collisionCheckObjects(player, objects, objectCount, dt);
        for (auto obj : disabled) {
            obj->m_isDisabled = false;
            obj->m_isDisabled2 = false;
        }
        disabled.clear();
    }

    bool canBeActivatedByPlayer(PlayerObject* player, EffectGameObject* object) {
        if (!ghostPtr(player)) return GJBaseGameLayer::canBeActivatedByPlayer(player, object);
        if (object) {
            if (!applySimplePortal(player, object->m_objectID) && interactiveType(object->m_objectType)) {
                g_stopped = true;
            }
        }
        return false;
    }

    void playerTouchedRing(PlayerObject* player, RingObject* ring) {
        if (ghostPtr(player)) {
            g_stopped = true;
            return;
        }
        GJBaseGameLayer::playerTouchedRing(player, ring);
    }

    void playerTouchedTrigger(PlayerObject* player, EffectGameObject* object) {
        if (ghostPtr(player)) {
            if (object) applySimplePortal(player, object->m_objectID);
            return;
        }
        GJBaseGameLayer::playerTouchedTrigger(player, object);
    }

    void bumpPlayer(PlayerObject* player, EffectGameObject* object) {
        if (ghostPtr(player)) {
            g_stopped = true;
            return;
        }
        GJBaseGameLayer::bumpPlayer(player, object);
    }

    void playerWillSwitchMode(PlayerObject* player, GameObject* object) {
        if (ghostPtr(player)) {
            g_stopped = true;
            return;
        }
        GJBaseGameLayer::playerWillSwitchMode(player, object);
    }

    void flipGravity(PlayerObject* player, bool flip, bool noEffects) {
        if (ghostPtr(player)) {
            if (g_simulating) g_stopped = true;
            return;
        }
        GJBaseGameLayer::flipGravity(player, flip, noEffects);
    }

    void teleportPlayer(TeleportPortalObject* object, PlayerObject* player) {
        if (ghostPtr(player)) {
            g_stopped = true;
            return;
        }
        GJBaseGameLayer::teleportPlayer(object, player);
    }

    void toggleDualMode(GameObject* object, bool dual, PlayerObject* player, bool noEffects) {
        if (ghostPtr(player)) {
            g_stopped = true;
            return;
        }
        GJBaseGameLayer::toggleDualMode(object, dual, player, noEffects);
    }

    void pickupItem(EffectGameObject* object) {
        if (g_simulating) return;
        GJBaseGameLayer::pickupItem(object);
    }

    void destroyObject(GameObject* object) {
        if (g_simulating) return;
        GJBaseGameLayer::destroyObject(object);
    }

    void gameEventTriggered(GJGameEvent event, int material, int playerID) {
        if (g_simulating) return;
        GJBaseGameLayer::gameEventTriggered(event, material, playerID);
    }

    void activateSFXTrigger(SFXTriggerGameObject* object) {
        if (g_simulating) return;
        GJBaseGameLayer::activateSFXTrigger(object);
    }

    void activateSongEditTrigger(SongTriggerGameObject* object) {
        if (g_simulating) return;
        GJBaseGameLayer::activateSongEditTrigger(object);
    }
};

class $modify(SimEnhancedObject, EnhancedGameObject) {
    void activatedByPlayer(PlayerObject* player) {
        if (ghostPtr(player)) return;
        EnhancedGameObject::activatedByPlayer(player);
    }
};

class $modify(SimGameObject, GameObject) {
    void playShineEffect() {
        if (g_simulating) return;
        GameObject::playShineEffect();
    }
};

class $modify(SimHardStreak, HardStreak) {
    void addPoint(CCPoint point) {
        if (g_simulating) return;
        HardStreak::addPoint(point);
    }
};

// ---- public API -----------------------------------------------------------------

namespace bm::sim {

void beginStep(GJBaseGameLayer* layer) {
    if (g_accumDt > 0.f) {
        g_stepDt = g_accumDt;
        float warp = layer ? layer->m_gameState.m_timeWarp : 1.f;
        g_tps.feed(logic::tpsFromStepDt(g_stepDt, warp));
    }
    g_accumDt = 0.f;
}

float stepDt() { return g_stepDt; }
int detectedTps() { return g_tps.value(); }
bool isGhost(PlayerObject* p) { return ghostPtr(p); }
bool simulating() { return g_simulating; }
uint64_t totalSteps() { return g_totalSteps; }

bool onDestroyPlayer(PlayerObject* p) {
    if (!ghostPtr(p)) return false;
    if (g_simulating) g_died = true;
    return true;
}

bool usable(PlayLayer* pl) {
    return pl && pl == PlayLayer::get() && pl->m_started && !pl->m_isPlatformer &&
           !pl->m_hasCompletedLevel && !pl->m_playerDied && g_stepDt > 0.f && !g_simulating;
}

void prepare(PlayLayer* pl) {
    if (!usable(pl)) return;
    static_cast<SimPlayLayer*>(pl)->ensureGhosts(2);
}

Outcome run(PlayLayer* pl, Request const& req) {
    if (!usable(pl) || req.count <= 0 || req.horizon <= 0) return Outcome::Unknown;
    int count = req.count > 2 ? 2 : req.count;
    for (int i = 0; i < count; i++) {
        if (!req.real[i] || req.real[i]->m_isDead) return Outcome::Unknown;
    }
    auto layer = static_cast<SimPlayLayer*>(pl);
    if (!layer->ensureGhosts(count)) return Outcome::Unknown;

    PlayerObject* ghosts[2] = {g_ghosts[0], g_ghosts[1]};
    bool airborne[2] = {false, false};
    bool landed[2] = {false, false};
    for (int i = 0; i < count; i++) copyState(ghosts[i], req.real[i]);

    float dt = g_stepDt;
    int lastEvent = req.plan.lastStep();
    Outcome result = Outcome::Survive;

    g_simulating = true;
    g_died = false;
    g_stopped = false;

    for (int step = 0; step < req.horizon; step++) {
        for (int e = 0; e < req.plan.count; e++) {
            if (req.plan.ev[e].step != step) continue;
            for (int i = 0; i < count; i++) {
                if (req.plan.ev[e].down) ghosts[i]->pushButton(PlayerButton::Jump);
                else ghosts[i]->releaseButton(PlayerButton::Jump);
            }
        }
        for (int i = 0; i < count; i++) ghosts[i]->update(dt);
        for (int i = 0; i < count; i++) {
            clearCollisionLog(ghosts[i]);
            pl->checkCollisions(ghosts[i], dt, false);
            ghosts[i]->updateRotation(dt);
        }
        g_totalSteps += static_cast<uint64_t>(count);

        if (g_died) {
            result = Outcome::Die;
            break;
        }
        if (g_stopped) {
            result = Outcome::Unknown;
            break;
        }
        if (req.stopOnLand && step >= lastEvent) {
            bool all = true;
            for (int i = 0; i < count; i++) {
                if (!ghosts[i]->m_isOnGround) airborne[i] = true;
                else if (airborne[i]) landed[i] = true;
                all = all && landed[i];
            }
            if (all) break;
        }
    }

    g_simulating = false;
    g_died = false;
    g_stopped = false;
    for (int i = 0; i < count; i++) {
        ghosts[i]->setVisible(false);
        if (ghosts[i]->m_touchingRings) ghosts[i]->m_touchingRings->removeAllObjects();
    }
    return result;
}

} // namespace bm::sim
