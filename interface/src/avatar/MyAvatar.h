//
//  MyAvatar.h
//  interface/src/avatar
//
//  Created by Mark Peng on 8/16/13.
//  Copyright 2012 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_MyAvatar_h
#define hifi_MyAvatar_h

#include <QSettings>

#include "Avatar.h"

enum AvatarHandState
{
    HAND_STATE_NULL = 0,
    HAND_STATE_OPEN,
    HAND_STATE_GRASPING,
    HAND_STATE_POINTING,
    NUM_HAND_STATES
};

class MyAvatar : public Avatar {
    Q_OBJECT
    Q_PROPERTY(bool shouldRenderLocally READ getShouldRenderLocally WRITE setShouldRenderLocally)
    Q_PROPERTY(quint32 motionBehaviors READ getMotionBehaviorsForScript WRITE setMotionBehaviorsByScript)
    Q_PROPERTY(glm::vec3 gravity READ getGravity WRITE setLocalGravity)

public:
	MyAvatar();
    ~MyAvatar();
    
    void reset();
    void update(float deltaTime);
    void simulate(float deltaTime);
    void updateFromTrackers(float deltaTime);
    void moveWithLean();

    void render(const glm::vec3& cameraPosition, RenderMode renderMode = NORMAL_RENDER_MODE);
    void renderBody(RenderMode renderMode, float glowLevel = 0.0f);
    bool shouldRenderHead(const glm::vec3& cameraPosition, RenderMode renderMode) const;
    void renderDebugBodyPoints();
    void renderHeadMouse(int screenWidth, int screenHeight) const;

    // setters
    void setMousePressed(bool mousePressed) { _mousePressed = mousePressed; }
    void setVelocity(const glm::vec3 velocity) { _velocity = velocity; }
    void setLeanScale(float scale) { _leanScale = scale; }
    void setLocalGravity(glm::vec3 gravity);
    void setShouldRenderLocally(bool shouldRender) { _shouldRender = shouldRender; }

    // getters
    float getLeanScale() const { return _leanScale; }
    const glm::vec3& getMouseRayOrigin() const { return _mouseRayOrigin; }
    const glm::vec3& getMouseRayDirection() const { return _mouseRayDirection; }
    glm::vec3 getGravity() const { return _gravity; }
    glm::vec3 getUprightHeadPosition() const;
    bool getShouldRenderLocally() const { return _shouldRender; }
    
    const QList<AnimationHandlePointer>& getAnimationHandles() const { return _animationHandles; }
    AnimationHandlePointer addAnimationHandle();
    void removeAnimationHandle(const AnimationHandlePointer& handle);
    
    /// Allows scripts to run animations.
    Q_INVOKABLE void startAnimation(const QString& url, float fps = 30.0f, float priority = 1.0f, bool loop = false,
        bool hold = false, int firstFrame = 0, int lastFrame = INT_MAX, const QStringList& maskedJoints = QStringList());
    
    /// Stops an animation as identified by a URL.
    Q_INVOKABLE void stopAnimation(const QString& url);
    
    /// Starts an animation by its role, using the provided URL and parameters if the avatar doesn't have a custom
    /// animation for the role.
    Q_INVOKABLE void startAnimationByRole(const QString& role, const QString& url = QString(), float fps = 30.0f,
        float priority = 1.0f, bool loop = false, bool hold = false, int firstFrame = 0,
        int lastFrame = INT_MAX, const QStringList& maskedJoints = QStringList());
    
    /// Stops an animation identified by its role.
    Q_INVOKABLE void stopAnimationByRole(const QString& role);
    
    // get/set avatar data
    void saveData(QSettings* settings);
    void loadData(QSettings* settings);

    void saveAttachmentData(const AttachmentData& attachment) const;
    AttachmentData loadAttachmentData(const QUrl& modelURL, const QString& jointName = QString()) const;

    //  Set what driving keys are being pressed to control thrust levels
    void setDriveKeys(int key, float val) { _driveKeys[key] = val; };
    bool getDriveKeys(int key) { return _driveKeys[key] != 0.f; };
    void jump() { _shouldJump = true; };
    
    bool isMyAvatar() { return true; }

    virtual int parseDataAtOffset(const QByteArray& packet, int offset);
    
    static void sendKillAvatar();

    Q_INVOKABLE glm::vec3 getTargetAvatarPosition() const { return _targetAvatarPosition; }
    AvatarData* getLookAtTargetAvatar() const { return _lookAtTargetAvatar.data(); }
    void updateLookAtTargetAvatar();
    void clearLookAtTargetAvatar();
    
    virtual void setJointData(int index, const glm::quat& rotation);
    virtual void clearJointData(int index);
    virtual void setFaceModelURL(const QUrl& faceModelURL);
    virtual void setSkeletonModelURL(const QUrl& skeletonModelURL);
    virtual void setAttachmentData(const QVector<AttachmentData>& attachmentData);
    
    virtual void attach(const QString& modelURL, const QString& jointName = QString(),
        const glm::vec3& translation = glm::vec3(), const glm::quat& rotation = glm::quat(), float scale = 1.0f,
        bool allowDuplicates = false, bool useSaved = true);
        
    virtual void setCollisionGroups(quint32 collisionGroups);

    void setMotionBehaviorsByScript(quint32 flags);
    quint32 getMotionBehaviorsForScript() const { return _motionBehaviors & AVATAR_MOTION_SCRIPTABLE_BITS; }

    void applyCollision(const glm::vec3& contactPoint, const glm::vec3& penetration);

public slots:
    void goHome();
    void increaseSize();
    void decreaseSize();
    void resetSize();
    
    void goToLocationFromResponse(const QJsonObject& jsonObject);

    //  Set/Get update the thrust that will move the avatar around
    void addThrust(glm::vec3 newThrust) { _thrust += newThrust; };
    glm::vec3 getThrust() { return _thrust; };
    void setThrust(glm::vec3 newThrust) { _thrust = newThrust; }

    void updateMotionBehaviorsFromMenu();

signals:
    void transformChanged();

protected:
    virtual void renderAttachments(RenderMode renderMode);
    
private:
    bool _mousePressed;
    float _bodyPitchDelta;  // degrees
    float _bodyRollDelta;   // degrees
    bool _shouldJump;
    float _driveKeys[MAX_DRIVE_KEYS];
    glm::vec3 _gravity;
    float _distanceToNearestAvatar; // How close is the nearest avatar?

    bool _wasPushing;
    bool _isPushing;
    bool _isBraking;
    float _trapDuration; // seconds that avatar has been trapped by collisions
    glm::vec3 _thrust;  // final acceleration from outside sources for the current frame

    glm::vec3 _motorVelocity;   // intended velocity of avatar motion
    float _motorTimescale;      // timescale for avatar motor to achieve its desired velocity
    float _maxMotorSpeed;
    quint32 _motionBehaviors;

    glm::vec3 _lastBodyPenetration;
    glm::vec3 _lastFloorContactPoint;
    QWeakPointer<AvatarData> _lookAtTargetAvatar;
    glm::vec3 _targetAvatarPosition;
    bool _shouldRender;
    bool _billboardValid;
    float _oculusYawOffset;

    QList<AnimationHandlePointer> _animationHandles;

	// private methods
    float computeDistanceToFloor(const glm::vec3& startPoint);
    void updateOrientation(float deltaTime);
    void updatePosition(float deltaTime);
    void updateMotorFromKeyboard(float deltaTime, bool walking);
    float computeMotorTimescale();
    void applyMotor(float deltaTime);
    void applyThrust(float deltaTime);
    void updateCollisionWithAvatars(float deltaTime);
    void updateCollisionWithEnvironment(float deltaTime, float radius);
    void updateCollisionWithVoxels(float deltaTime, float radius);
    void applyHardCollision(const glm::vec3& penetration, float elasticity, float damping);
    void updateCollisionSound(const glm::vec3& penetration, float deltaTime, float frequency);
    void updateChatCircle(float deltaTime);
    void maybeUpdateBillboard();
    void setGravity(const glm::vec3& gravity);
};

#endif // hifi_MyAvatar_h
