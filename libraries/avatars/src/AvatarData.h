//
//  AvatarData.h
//  libraries/avatars/src
//
//  Created by Stephen Birarda on 4/9/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_AvatarData_h
#define hifi_AvatarData_h

#include <string>
/* VS2010 defines stdint.h, but not inttypes.h */
#if defined(_MSC_VER)
typedef signed char  int8_t;
typedef signed short int16_t;
typedef signed int   int32_t;
typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef signed long long   int64_t;
typedef unsigned long long quint64;
#define PRId64 "I64d"
#else
#include <inttypes.h>
#endif
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <QtCore/QElapsedTimer>
#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtCore/QVariantMap>
#include <QRect>
#include <QScriptable>
#include <QUuid>

#include <CollisionInfo.h>
#include <RegisteredMetaTypes.h>

#include <Node.h>

#include "HeadData.h"
#include "HandData.h"

// avatar motion behaviors
const quint32 AVATAR_MOTION_MOTOR_ENABLED = 1U << 0;
const quint32 AVATAR_MOTION_MOTOR_KEYBOARD_ENABLED = 1U << 1;
const quint32 AVATAR_MOTION_MOTOR_USE_LOCAL_FRAME = 1U << 2;
const quint32 AVATAR_MOTION_MOTOR_COLLISION_SURFACE_ONLY = 1U << 3;

const quint32 AVATAR_MOTION_OBEY_ENVIRONMENTAL_GRAVITY = 1U << 4;
const quint32 AVATAR_MOTION_OBEY_LOCAL_GRAVITY = 1U << 5;

const quint32 AVATAR_MOTION_STAND_ON_NEARBY_FLOORS = 1U << 6;

const quint32 AVATAR_MOTION_DEFAULTS = 
        AVATAR_MOTION_MOTOR_ENABLED |
        AVATAR_MOTION_MOTOR_KEYBOARD_ENABLED |
        AVATAR_MOTION_MOTOR_USE_LOCAL_FRAME |
        AVATAR_MOTION_STAND_ON_NEARBY_FLOORS;

// these bits will be expanded as features are exposed
const quint32 AVATAR_MOTION_SCRIPTABLE_BITS = 
        AVATAR_MOTION_OBEY_ENVIRONMENTAL_GRAVITY | 
        AVATAR_MOTION_OBEY_LOCAL_GRAVITY | 
        AVATAR_MOTION_STAND_ON_NEARBY_FLOORS;


// First bitset
const int KEY_STATE_START_BIT = 0; // 1st and 2nd bits
const int HAND_STATE_START_BIT = 2; // 3rd and 4th bits
const int IS_FACESHIFT_CONNECTED = 4; // 5th bit
const int IS_CHAT_CIRCLING_ENABLED = 5;

static const float MAX_AVATAR_SCALE = 1000.f;
static const float MIN_AVATAR_SCALE = .005f;

const float MAX_AUDIO_LOUDNESS = 1000.0; // close enough for mouth animation

const int AVATAR_IDENTITY_PACKET_SEND_INTERVAL_MSECS = 1000;
const int AVATAR_BILLBOARD_PACKET_SEND_INTERVAL_MSECS = 5000;

const QUrl DEFAULT_HEAD_MODEL_URL = QUrl("http://public.highfidelity.io/meshes/defaultAvatar_head.fst");
const QUrl DEFAULT_BODY_MODEL_URL = QUrl("http://public.highfidelity.io/meshes/defaultAvatar_body.fst");

enum KeyState {
    NO_KEY_DOWN = 0,
    INSERT_KEY_DOWN,
    DELETE_KEY_DOWN
};

const glm::vec3 vec3Zero(0.0f);

class QDataStream;
class QNetworkAccessManager;

class AttachmentData;
class JointData;

class AvatarData : public QObject {
    Q_OBJECT

    Q_PROPERTY(glm::vec3 position READ getPosition WRITE setPosition)
    Q_PROPERTY(float scale READ getTargetScale WRITE setTargetScale)
    Q_PROPERTY(glm::vec3 handPosition READ getHandPosition WRITE setHandPosition)
    Q_PROPERTY(float bodyYaw READ getBodyYaw WRITE setBodyYaw)
    Q_PROPERTY(float bodyPitch READ getBodyPitch WRITE setBodyPitch)
    Q_PROPERTY(float bodyRoll READ getBodyRoll WRITE setBodyRoll)
    Q_PROPERTY(QString chatMessage READ getQStringChatMessage WRITE setChatMessage)

    Q_PROPERTY(glm::quat orientation READ getOrientation WRITE setOrientation)
    Q_PROPERTY(glm::quat headOrientation READ getHeadOrientation WRITE setHeadOrientation)
    Q_PROPERTY(float headPitch READ getHeadPitch WRITE setHeadPitch)

    Q_PROPERTY(float audioLoudness READ getAudioLoudness WRITE setAudioLoudness)
    Q_PROPERTY(float audioAverageLoudness READ getAudioAverageLoudness WRITE setAudioAverageLoudness)

    Q_PROPERTY(QString displayName READ getDisplayName WRITE setDisplayName)
    Q_PROPERTY(QString faceModelURL READ getFaceModelURLFromScript WRITE setFaceModelURLFromScript)
    Q_PROPERTY(QString skeletonModelURL READ getSkeletonModelURLFromScript WRITE setSkeletonModelURLFromScript)
    Q_PROPERTY(QVector<AttachmentData> attachmentData READ getAttachmentData WRITE setAttachmentData)
    Q_PROPERTY(QString billboardURL READ getBillboardURL WRITE setBillboardFromURL)

    Q_PROPERTY(QStringList jointNames READ getJointNames)

    Q_PROPERTY(QUuid sessionUUID READ getSessionUUID)
public:
    AvatarData();
    virtual ~AvatarData();

    const QUuid& getSessionUUID() { return _sessionUUID; }

    const glm::vec3& getPosition() const { return _position; }
    void setPosition(const glm::vec3 position) { _position = position; }

    glm::vec3 getHandPosition() const;
    void setHandPosition(const glm::vec3& handPosition);

    QByteArray toByteArray();

    /// \return true if an error should be logged
    bool shouldLogError(const quint64& now);

    /// \param packet byte array of data
    /// \param offset number of bytes into packet where data starts
    /// \return number of bytes parsed
    virtual int parseDataAtOffset(const QByteArray& packet, int offset);

    //  Body Rotation (degrees)
    float getBodyYaw() const { return _bodyYaw; }
    void setBodyYaw(float bodyYaw) { _bodyYaw = bodyYaw; }
    float getBodyPitch() const { return _bodyPitch; }
    void setBodyPitch(float bodyPitch) { _bodyPitch = bodyPitch; }
    float getBodyRoll() const { return _bodyRoll; }
    void setBodyRoll(float bodyRoll) { _bodyRoll = bodyRoll; }

    glm::quat getOrientation() const { return glm::quat(glm::radians(glm::vec3(_bodyPitch, _bodyYaw, _bodyRoll))); }
    void setOrientation(const glm::quat& orientation);

    glm::quat getHeadOrientation() const { return _headData->getOrientation(); }
    void setHeadOrientation(const glm::quat& orientation) { _headData->setOrientation(orientation); }

    // access to Head().set/getMousePitch (degrees)
    float getHeadPitch() const { return _headData->getBasePitch(); }
    void setHeadPitch(float value) { _headData->setBasePitch(value); };

    // access to Head().set/getAverageLoudness
    float getAudioLoudness() const { return _headData->getAudioLoudness(); }
    void setAudioLoudness(float value) { _headData->setAudioLoudness(value); }
    float getAudioAverageLoudness() const { return _headData->getAudioAverageLoudness(); }
    void setAudioAverageLoudness(float value) { _headData->setAudioAverageLoudness(value); }

    //  Scale
    float getTargetScale() const { return _targetScale; }
    void setTargetScale(float targetScale) { _targetScale = targetScale; }
    void setClampedTargetScale(float targetScale);

    //  Hand State
    void setHandState(char s) { _handState = s; }
    char getHandState() const { return _handState; }

    const QVector<JointData>& getJointData() const { return _jointData; }
    void setJointData(const QVector<JointData>& jointData) { _jointData = jointData; }

    Q_INVOKABLE virtual void setJointData(int index, const glm::quat& rotation);
    Q_INVOKABLE virtual void clearJointData(int index);
    Q_INVOKABLE bool isJointDataValid(int index) const;
    Q_INVOKABLE virtual glm::quat getJointRotation(int index) const;

    Q_INVOKABLE void setJointData(const QString& name, const glm::quat& rotation);
    Q_INVOKABLE void clearJointData(const QString& name);
    Q_INVOKABLE bool isJointDataValid(const QString& name) const;
    Q_INVOKABLE glm::quat getJointRotation(const QString& name) const;

    /// Returns the index of the joint with the specified name, or -1 if not found/unknown.
    Q_INVOKABLE virtual int getJointIndex(const QString& name) const { return _jointIndices.value(name) - 1; } 

    Q_INVOKABLE virtual QStringList getJointNames() const { return _jointNames; }

    // key state
    void setKeyState(KeyState s) { _keyState = s; }
    KeyState keyState() const { return _keyState; }

    // chat message
    void setChatMessage(const std::string& msg) { _chatMessage = msg; }
    void setChatMessage(const QString& string) { _chatMessage = string.toLocal8Bit().constData(); }
    const std::string& setChatMessage() const { return _chatMessage; }
    QString getQStringChatMessage() { return QString(_chatMessage.data()); }

    bool isChatCirclingEnabled() const { return _isChatCirclingEnabled; }
    const HeadData* getHeadData() const { return _headData; }
    const HandData* getHandData() const { return _handData; }

    virtual const glm::vec3& getVelocity() const { return vec3Zero; }

    virtual bool findParticleCollisions(const glm::vec3& particleCenter, float particleRadius, CollisionList& collisions) {
        return false;
    }

    bool hasIdentityChangedAfterParsing(const QByteArray& packet);
    QByteArray identityByteArray();
    
    bool hasBillboardChangedAfterParsing(const QByteArray& packet);
    
    const QUrl& getFaceModelURL() const { return _faceModelURL; }
    QString getFaceModelURLString() const { return _faceModelURL.toString(); }
    const QUrl& getSkeletonModelURL() const { return _skeletonModelURL; }
    const QString& getDisplayName() const { return _displayName; }
    virtual void setFaceModelURL(const QUrl& faceModelURL);
    virtual void setSkeletonModelURL(const QUrl& skeletonModelURL);
    
    virtual void setDisplayName(const QString& displayName);
    
    Q_INVOKABLE QVector<AttachmentData> getAttachmentData() const;
    Q_INVOKABLE virtual void setAttachmentData(const QVector<AttachmentData>& attachmentData);
    
    Q_INVOKABLE virtual void attach(const QString& modelURL, const QString& jointName = QString(),
        const glm::vec3& translation = glm::vec3(), const glm::quat& rotation = glm::quat(), float scale = 1.0f,
        bool allowDuplicates = false, bool useSaved = true);
    
    Q_INVOKABLE void detachOne(const QString& modelURL, const QString& jointName = QString());
    Q_INVOKABLE void detachAll(const QString& modelURL, const QString& jointName = QString());
    
    virtual void setBillboard(const QByteArray& billboard);
    const QByteArray& getBillboard() const { return _billboard; }
    
    void setBillboardFromURL(const QString& billboardURL);
    const QString& getBillboardURL() { return _billboardURL; }
    
    QString getFaceModelURLFromScript() const { return _faceModelURL.toString(); }
    void setFaceModelURLFromScript(const QString& faceModelString) { setFaceModelURL(faceModelString); }
    
    QString getSkeletonModelURLFromScript() const { return _skeletonModelURL.toString(); }
    void setSkeletonModelURLFromScript(const QString& skeletonModelString) { setSkeletonModelURL(QUrl(skeletonModelString)); }
    
    Node* getOwningAvatarMixer() { return _owningAvatarMixer.data(); }
    void setOwningAvatarMixer(const QWeakPointer<Node>& owningAvatarMixer) { _owningAvatarMixer = owningAvatarMixer; }
    
    QElapsedTimer& getLastUpdateTimer() { return _lastUpdateTimer; }
     
    virtual float getBoundingRadius() const { return 1.f; }
    
    static void setNetworkAccessManager(QNetworkAccessManager* sharedAccessManager) { networkAccessManager = sharedAccessManager; }

public slots:
    void sendIdentityPacket();
    void sendBillboardPacket();
    void setBillboardFromNetworkReply();
    void setJointMappingsFromNetworkReply();
    void setSessionUUID(const QUuid& id) { _sessionUUID = id; }
protected:
    QUuid _sessionUUID;
    glm::vec3 _position;
    glm::vec3 _handPosition;

    //  Body rotation
    float _bodyYaw;     // degrees
    float _bodyPitch;   // degrees
    float _bodyRoll;    // degrees

    // Body scale
    float _targetScale;

    //  Hand state (are we grabbing something or not)
    char _handState;

    QVector<JointData> _jointData; ///< the state of the skeleton joints

    // key state
    KeyState _keyState;

    // chat message
    std::string _chatMessage;

    bool _isChatCirclingEnabled;

    bool _hasNewJointRotations; // set in AvatarData, cleared in Avatar

    HeadData* _headData;
    HandData* _handData;

    QUrl _faceModelURL;
    QUrl _skeletonModelURL;
    QVector<AttachmentData> _attachmentData;
    QString _displayName;

    QRect _displayNameBoundingRect;
    float _displayNameTargetAlpha;
    float _displayNameAlpha;

    QByteArray _billboard;
    QString _billboardURL;
    
    QHash<QString, int> _jointIndices; ///< 1-based, since zero is returned for missing keys
    QStringList _jointNames; ///< in order of depth-first traversal
    
    static QNetworkAccessManager* networkAccessManager;

    quint64 _errorLogExpiry; ///< time in future when to log an error
    
    QWeakPointer<Node> _owningAvatarMixer;
    QElapsedTimer _lastUpdateTimer;
    
    /// Loads the joint indices, names from the FST file (if any)
    virtual void updateJointMappings();

private:
    // privatize the copy constructor and assignment operator so they cannot be called
    AvatarData(const AvatarData&);
    AvatarData& operator= (const AvatarData&);
};

class JointData {
public:
    bool valid;
    glm::quat rotation;
};

class AttachmentData {
public:
    QUrl modelURL;
    QString jointName;
    glm::vec3 translation;
    glm::quat rotation;
    float scale;
    
    AttachmentData();
    
    bool isValid() const { return modelURL.isValid(); }
    
    bool operator==(const AttachmentData& other) const;
};

QDataStream& operator<<(QDataStream& out, const AttachmentData& attachment);
QDataStream& operator>>(QDataStream& in, AttachmentData& attachment);

Q_DECLARE_METATYPE(AttachmentData)
Q_DECLARE_METATYPE(QVector<AttachmentData>)

/// Scriptable wrapper for attachments.
class AttachmentDataObject : public QObject, protected QScriptable {
    Q_OBJECT
    Q_PROPERTY(QString modelURL READ getModelURL WRITE setModelURL)
    Q_PROPERTY(QString jointName READ getJointName WRITE setJointName)
    Q_PROPERTY(glm::vec3 translation READ getTranslation WRITE setTranslation)
    Q_PROPERTY(glm::quat rotation READ getRotation WRITE setRotation)
    Q_PROPERTY(float scale READ getScale WRITE setScale)

public:
    
    Q_INVOKABLE void setModelURL(const QString& modelURL) const;
    Q_INVOKABLE QString getModelURL() const;
    
    Q_INVOKABLE void setJointName(const QString& jointName) const;
    Q_INVOKABLE QString getJointName() const;
    
    Q_INVOKABLE void setTranslation(const glm::vec3& translation) const;
    Q_INVOKABLE glm::vec3 getTranslation() const;
    
    Q_INVOKABLE void setRotation(const glm::quat& rotation) const;
    Q_INVOKABLE glm::quat getRotation() const;
    
    Q_INVOKABLE void setScale(float scale) const;
    Q_INVOKABLE float getScale() const;
};

void registerAvatarTypes(QScriptEngine* engine);

#endif // hifi_AvatarData_h
