//
//  Avatar.h
//  interface/src/avatar
//
//  Copyright 2012 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Avatar_h
#define hifi_Avatar_h

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <QtCore/QScopedPointer>
#include <QtCore/QUuid>

#include <AvatarData.h>

#include "Hand.h"
#include "Head.h"
#include "InterfaceConfig.h"
#include "SkeletonModel.h"
#include "world.h"

static const float SCALING_RATIO = .05f;
static const float SMOOTHING_RATIO = .05f; // 0 < ratio < 1
static const float RESCALING_TOLERANCE = .02f;

extern const float CHAT_MESSAGE_SCALE;
extern const float CHAT_MESSAGE_HEIGHT;

enum DriveKeys {
    FWD = 0,
    BACK,
    LEFT,
    RIGHT,
    UP,
    DOWN,
    ROT_LEFT,
    ROT_RIGHT,
    ROT_UP,
    ROT_DOWN,
    MAX_DRIVE_KEYS
};

enum ScreenTintLayer {
    SCREEN_TINT_BEFORE_LANDSCAPE = 0,
    SCREEN_TINT_BEFORE_AVATARS,
    SCREEN_TINT_BEFORE_MY_AVATAR,
    SCREEN_TINT_AFTER_AVATARS,
    NUM_SCREEN_TINT_LAYERS
};

// Where one's own Avatar begins in the world (will be overwritten if avatar data file is found).
// This is the start location in the Sandbox (xyz: 6270, 211, 6000).
const glm::vec3 START_LOCATION(0.38269043f * TREE_SCALE, 0.01287842f * TREE_SCALE, 0.36621094f * TREE_SCALE);

class Texture;

class Avatar : public AvatarData {
    Q_OBJECT
    Q_PROPERTY(quint32 collisionGroups READ getCollisionGroups WRITE setCollisionGroups)

public:
    Avatar();
    ~Avatar();

    void init();
    void simulate(float deltaTime);
    
    enum RenderMode { NORMAL_RENDER_MODE, SHADOW_RENDER_MODE, MIRROR_RENDER_MODE };
    
    virtual void render(const glm::vec3& cameraPosition, RenderMode renderMode = NORMAL_RENDER_MODE);

    //setters
    void setDisplayingLookatVectors(bool displayingLookatVectors) { getHead()->setRenderLookatVectors(displayingLookatVectors); }
    void setMouseRay(const glm::vec3 &origin, const glm::vec3 &direction);
    void setIsLookAtTarget(const bool isLookAtTarget) { _isLookAtTarget = isLookAtTarget; }
    //getters
    bool isInitialized() const { return _initialized; }
    SkeletonModel& getSkeletonModel() { return _skeletonModel; }
    const QVector<Model*>& getAttachmentModels() const { return _attachmentModels; }
    glm::vec3 getChestPosition() const;
    float getScale() const { return _scale; }
    const glm::vec3& getVelocity() const { return _velocity; }
    const Head* getHead() const { return static_cast<const Head*>(_headData); }
    Head* getHead() { return static_cast<Head*>(_headData); }
    Hand* getHand() { return static_cast<Hand*>(_handData); }
    glm::quat getWorldAlignedOrientation() const;

    /// Returns the distance to use as a LOD parameter.
    float getLODDistance() const;

    bool findRayIntersection(const glm::vec3& origin, const glm::vec3& direction, float& distance) const;

    /// \param shapes list of shapes to collide against avatar
    /// \param collisions list to store collision results
    /// \return true if at least one shape collided with avatar
    bool findCollisions(const QVector<const Shape*>& shapes, CollisionList& collisions);

    /// Checks for penetration between the described sphere and the avatar.
    /// \param penetratorCenter the center of the penetration test sphere
    /// \param penetratorRadius the radius of the penetration test sphere
    /// \param collisions[out] a list to which collisions get appended
    /// \param skeletonSkipIndex if not -1, the index of a joint to skip (along with its descendents) in the skeleton model
    /// \return whether or not the sphere penetrated
    bool findSphereCollisions(const glm::vec3& penetratorCenter, float penetratorRadius,
        CollisionList& collisions, int skeletonSkipIndex = -1);

    /// Checks for penetration between the described plane and the avatar.
    /// \param plane the penetration plane
    /// \param collisions[out] a list to which collisions get appended
    /// \return whether or not the plane penetrated
    bool findPlaneCollisions(const glm::vec4& plane, CollisionList& collisions);

    /// Checks for collision between the a spherical particle and the avatar (including paddle hands)
    /// \param collisionCenter the center of particle's bounding sphere
    /// \param collisionRadius the radius of particle's bounding sphere
    /// \param collisions[out] a list to which collisions get appended
    /// \return whether or not the particle collided
    bool findParticleCollisions(const glm::vec3& particleCenter, float particleRadius, CollisionList& collisions);

    virtual bool isMyAvatar() { return false; }
    
    virtual glm::quat getJointRotation(int index) const;
    virtual int getJointIndex(const QString& name) const;
    virtual QStringList getJointNames() const;
    
    virtual void setFaceModelURL(const QUrl& faceModelURL);
    virtual void setSkeletonModelURL(const QUrl& skeletonModelURL);
    virtual void setAttachmentData(const QVector<AttachmentData>& attachmentData);
    virtual void setDisplayName(const QString& displayName);
    virtual void setBillboard(const QByteArray& billboard);

    void setShowDisplayName(bool showDisplayName);
    
    virtual int parseDataAtOffset(const QByteArray& packet, int offset);

    static void renderJointConnectingCone(glm::vec3 position1, glm::vec3 position2, float radius1, float radius2);

    /// \return true if we expect the avatar would move as a result of the collision
    bool collisionWouldMoveAvatar(CollisionInfo& collision) const;

    virtual void applyCollision(const glm::vec3& contactPoint, const glm::vec3& penetration) { }

    /// \return bounding radius of avatar
    virtual float getBoundingRadius() const;
    void updateShapePositions();

    quint32 getCollisionGroups() const { return _collisionGroups; }
    virtual void setCollisionGroups(quint32 collisionGroups) { _collisionGroups = (collisionGroups & VALID_COLLISION_GROUPS); }

public slots:
    void updateCollisionGroups();

signals:
    void collisionWithAvatar(const QUuid& myUUID, const QUuid& theirUUID, const CollisionInfo& collision);

protected:
    SkeletonModel _skeletonModel;
    QVector<Model*> _attachmentModels;
    float _bodyYawDelta;
    glm::vec3 _velocity;
    float _leanScale;
    float _scale;
    glm::vec3 _worldUpDirection;
    glm::vec3 _mouseRayOrigin;
    glm::vec3 _mouseRayDirection;
    float _stringLength;
    bool _moving; ///< set when position is changing

    quint32 _collisionGroups;

    // protected methods...
    glm::vec3 getBodyRightDirection() const { return getOrientation() * IDENTITY_RIGHT; }
    glm::vec3 getBodyUpDirection() const { return getOrientation() * IDENTITY_UP; }
    glm::vec3 getBodyFrontDirection() const { return getOrientation() * IDENTITY_FRONT; }
    glm::quat computeRotationFromBodyToWorldUp(float proportion = 1.0f) const;
    void setScale(float scale);

    float getSkeletonHeight() const;
    float getHeadHeight() const;
    float getPelvisFloatingHeight() const;
    float getPelvisToHeadLength() const;
    glm::vec3 getDisplayNamePosition();

    void renderDisplayName();
    virtual void renderBody(RenderMode renderMode, float glowLevel = 0.0f);
    virtual bool shouldRenderHead(const glm::vec3& cameraPosition, RenderMode renderMode) const;

    void simulateAttachments(float deltaTime);
    virtual void renderAttachments(RenderMode renderMode);

    virtual void updateJointMappings();

private:

    bool _initialized;
    QScopedPointer<Texture> _billboardTexture;
    bool _shouldRenderBillboard;
    bool _isLookAtTarget;

    void renderBillboard();
    
    float getBillboardSize() const;
};

#endif // hifi_Avatar_h
