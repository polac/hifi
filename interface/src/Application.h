//
//  Application.h
//  interface/src
//
//  Created by Andrzej Kapolka on 5/10/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_Application_h
#define hifi_Application_h

#include <map>
#include <time.h>

#include <QApplication>
#include <QMainWindow>
#include <QAction>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QHash>
#include <QTouchEvent>
#include <QUndoStack>
#include <QSystemTrayIcon>

#include <ModelEditPacketSender.h>
#include <NetworkPacket.h>
#include <NodeList.h>
#include <PacketHeaders.h>
#include <ParticleCollisionSystem.h>
#include <ParticleEditPacketSender.h>
#include <ScriptEngine.h>
#include <OctreeQuery.h>
#include <ViewFrustum.h>
#include <VoxelEditPacketSender.h>

#include "MainWindow.h"
#include "Audio.h"
#include "AudioReflector.h"
#include "BuckyBalls.h"
#include "Camera.h"
#include "DatagramProcessor.h"
#include "Environment.h"
#include "FileLogger.h"
#include "GLCanvas.h"
#include "Menu.h"
#include "MetavoxelSystem.h"
#include "PacketHeaders.h"
#include "Stars.h"
#include "avatar/Avatar.h"
#include "avatar/AvatarManager.h"
#include "avatar/MyAvatar.h"
#include "devices/Faceplus.h"
#include "devices/Faceshift.h"
#include "devices/JoystickManager.h"
#include "devices/PrioVR.h"
#include "devices/SixenseManager.h"
#include "devices/Visage.h"
#include "models/ModelTreeRenderer.h"
#include "particles/ParticleTreeRenderer.h"
#include "renderer/AmbientOcclusionEffect.h"
#include "renderer/GeometryCache.h"
#include "renderer/GlowEffect.h"
#include "renderer/PointShader.h"
#include "renderer/TextureCache.h"
#include "renderer/VoxelShader.h"
#include "scripting/ControllerScriptingInterface.h"
#include "ui/BandwidthDialog.h"
#include "ui/BandwidthMeter.h"
#include "ui/ModelsBrowser.h"
#include "ui/NodeBounds.h"
#include "ui/OctreeStatsDialog.h"
#include "ui/RearMirrorTools.h"
#include "ui/SnapshotShareDialog.h"
#include "ui/LodToolsDialog.h"
#include "ui/LogDialog.h"
#include "ui/UpdateDialog.h"
#include "ui/overlays/Overlays.h"
#include "ui/ApplicationOverlay.h"
#include "ui/RunningScriptsWidget.h"
#include "voxels/VoxelFade.h"
#include "voxels/VoxelHideShowThread.h"
#include "voxels/VoxelImporter.h"
#include "voxels/VoxelPacketProcessor.h"
#include "voxels/VoxelSystem.h"


class QAction;
class QActionGroup;
class QGLWidget;
class QKeyEvent;
class QMouseEvent;
class QNetworkAccessManager;
class QSettings;
class QWheelEvent;

class Node;
class ProgramObject;

static const float NODE_ADDED_RED   = 0.0f;
static const float NODE_ADDED_GREEN = 1.0f;
static const float NODE_ADDED_BLUE  = 0.0f;
static const float NODE_KILLED_RED   = 1.0f;
static const float NODE_KILLED_GREEN = 0.0f;
static const float NODE_KILLED_BLUE  = 0.0f;

static const QString SNAPSHOT_EXTENSION  = ".jpg";
static const QString CUSTOM_URL_SCHEME = "hifi:";

static const float BILLBOARD_FIELD_OF_VIEW = 30.0f; // degrees
static const float BILLBOARD_DISTANCE = 5.0f;       // meters

static const int MIRROR_VIEW_TOP_PADDING = 5;
static const int MIRROR_VIEW_LEFT_PADDING = 10;
static const int MIRROR_VIEW_WIDTH = 265;
static const int MIRROR_VIEW_HEIGHT = 215;
static const float MIRROR_FULLSCREEN_DISTANCE = 0.35f;
static const float MIRROR_REARVIEW_DISTANCE = 0.65f;
static const float MIRROR_REARVIEW_BODY_DISTANCE = 2.3f;
static const float MIRROR_FIELD_OF_VIEW = 30.0f;

class Application : public QApplication {
    Q_OBJECT

    friend class VoxelPacketProcessor;
    friend class VoxelEditPacketSender;
    friend class DatagramProcessor;

public:
    static Application* getInstance() { return static_cast<Application*>(QCoreApplication::instance()); }
    static QString& resourcesPath();

    Application(int& argc, char** argv, QElapsedTimer &startup_time);
    ~Application();

    void restoreSizeAndPosition();
    void loadScripts();
    QString getPreviousScriptLocation();
    void setPreviousScriptLocation(const QString& previousScriptLocation);
    void storeSizeAndPosition();
    void clearScriptsBeforeRunning();
    void saveScripts();
    void initializeGL();
    void paintGL();
    void resizeGL(int width, int height);
    void urlGoTo(int argc, const char * constArgv[]);

    void keyPressEvent(QKeyEvent* event);
    void keyReleaseEvent(QKeyEvent* event);

    void focusOutEvent(QFocusEvent* event);
    void focusInEvent(QFocusEvent* event);

    void mouseMoveEvent(QMouseEvent* event);
    void mousePressEvent(QMouseEvent* event);
    void mouseReleaseEvent(QMouseEvent* event);

    void touchBeginEvent(QTouchEvent* event);
    void touchEndEvent(QTouchEvent* event);
    void touchUpdateEvent(QTouchEvent* event);

    void wheelEvent(QWheelEvent* event);
    void dropEvent(QDropEvent *event);

    bool event(QEvent* event);

    void makeVoxel(glm::vec3 position,
                   float scale,
                   unsigned char red,
                   unsigned char green,
                   unsigned char blue,
                   bool isDestructive);

    void removeVoxel(glm::vec3 position, float scale);

    glm::vec3 getMouseVoxelWorldCoordinates(const VoxelDetail& mouseVoxel);

    QGLWidget* getGLWidget() { return _glWidget; }
    bool isThrottleRendering() const { return _glWidget->isThrottleRendering(); }
    MyAvatar* getAvatar() { return _myAvatar; }
    Audio* getAudio() { return &_audio; }
    const AudioReflector* getAudioReflector() const { return &_audioReflector; }
    Camera* getCamera() { return &_myCamera; }
    ViewFrustum* getViewFrustum() { return &_viewFrustum; }
    ViewFrustum* getShadowViewFrustum() { return &_shadowViewFrustum; }
    VoxelSystem* getVoxels() { return &_voxels; }
    VoxelTree* getVoxelTree() { return _voxels.getTree(); }
    const VoxelPacketProcessor& getVoxelPacketProcessor() const { return _voxelProcessor; }
    ParticleTreeRenderer* getParticles() { return &_particles; }
    MetavoxelSystem* getMetavoxels() { return &_metavoxels; }
    ModelTreeRenderer* getModels() { return &_models; }
    bool getImportSucceded() { return _importSucceded; }
    VoxelSystem* getSharedVoxelSystem() { return &_sharedVoxelSystem; }
    VoxelTree* getClipboard() { return &_clipboard; }
    Environment* getEnvironment() { return &_environment; }
    bool isMousePressed() const { return _mousePressed; }
    bool isMouseHidden() const { return _mouseHidden; }
    const glm::vec3& getMouseRayOrigin() const { return _mouseRayOrigin; }
    const glm::vec3& getMouseRayDirection() const { return _mouseRayDirection; }
    int getMouseX() const { return _mouseX; }
    int getMouseY() const { return _mouseY; }
    Faceplus* getFaceplus() { return &_faceplus; }
    Faceshift* getFaceshift() { return &_faceshift; }
    Visage* getVisage() { return &_visage; }
    FaceTracker* getActiveFaceTracker();
    SixenseManager* getSixenseManager() { return &_sixenseManager; }
    PrioVR* getPrioVR() { return &_prioVR; }
    JoystickManager* getJoystickManager() { return &_joystickManager; }
    BandwidthMeter* getBandwidthMeter() { return &_bandwidthMeter; }
    QUndoStack* getUndoStack() { return &_undoStack; }
    QSystemTrayIcon* getTrayIcon() { return _trayIcon; }
    ApplicationOverlay& getApplicationOverlay() { return _applicationOverlay; }
    Overlays& getOverlays() { return _overlays; }

    float getFps() const { return _fps; }
    float getPacketsPerSecond() const { return _packetsPerSecond; }
    float getBytesPerSecond() const { return _bytesPerSecond; }
    const glm::vec3& getViewMatrixTranslation() const { return _viewMatrixTranslation; }

    /// if you need to access the application settings, use lockSettings()/unlockSettings()
    QSettings* lockSettings() { _settingsMutex.lock(); return _settings; }
    void unlockSettings() { _settingsMutex.unlock(); }

    void saveSettings();

    MainWindow* getWindow() { return _window; }
    NodeToOctreeSceneStats* getOcteeSceneStats() { return &_octreeServerSceneStats; }
    void lockOctreeSceneStats() { _octreeSceneStatsLock.lockForRead(); }
    void unlockOctreeSceneStats() { _octreeSceneStatsLock.unlock(); }

    QNetworkAccessManager* getNetworkAccessManager() { return _networkAccessManager; }
    GeometryCache* getGeometryCache() { return &_geometryCache; }
    AnimationCache* getAnimationCache() { return &_animationCache; }
    TextureCache* getTextureCache() { return &_textureCache; }
    GlowEffect* getGlowEffect() { return &_glowEffect; }
    ControllerScriptingInterface* getControllerScriptingInterface() { return &_controllerScriptingInterface; }

    AvatarManager& getAvatarManager() { return _avatarManager; }
    void resetProfile(const QString& username);

    void controlledBroadcastToNodes(const QByteArray& packet, const NodeSet& destinationNodeTypes);

    void setupWorldLight();

    QImage renderAvatarBillboard();

    void displaySide(Camera& whichCamera, bool selfAvatarOnly = false);

    /// Stores the current modelview matrix as the untranslated view matrix to use for transforms and the supplied vector as
    /// the view matrix translation.
    void updateUntranslatedViewMatrix(const glm::vec3& viewMatrixTranslation = glm::vec3());

    /// Loads a view matrix that incorporates the specified model translation without the precision issues that can
    /// result from matrix multiplication at high translation magnitudes.
    void loadTranslatedViewMatrix(const glm::vec3& translation);

    const glm::mat4& getShadowMatrix() const { return _shadowMatrix; }

    void getModelViewMatrix(glm::dmat4* modelViewMatrix);
    void getProjectionMatrix(glm::dmat4* projectionMatrix);

    /// Computes the off-axis frustum parameters for the view frustum, taking mirroring into account.
    void computeOffAxisFrustum(float& left, float& right, float& bottom, float& top, float& nearVal,
        float& farVal, glm::vec4& nearClipPlane, glm::vec4& farClipPlane) const;

    NodeBounds& getNodeBoundsDisplay()  { return _nodeBoundsDisplay; }

    VoxelShader& getVoxelShader() { return _voxelShader; }
    PointShader& getPointShader() { return _pointShader; }
    FileLogger* getLogger() { return _logger; }

    glm::vec2 getViewportDimensions() const{ return glm::vec2(_glWidget->width(),_glWidget->height()); }
    NodeToJurisdictionMap& getVoxelServerJurisdictions() { return _voxelServerJurisdictions; }
    NodeToJurisdictionMap& getParticleServerJurisdictions() { return _particleServerJurisdictions; }
    NodeToJurisdictionMap& getModelServerJurisdictions() { return _modelServerJurisdictions; }
    void pasteVoxelsToOctalCode(const unsigned char* octalCodeDestination);

    void skipVersion(QString latestVersion);

    QStringList getRunningScripts() { return _scriptEnginesHash.keys(); }
    ScriptEngine* getScriptEngine(QString scriptHash) { return _scriptEnginesHash.contains(scriptHash) ? _scriptEnginesHash[scriptHash] : NULL; }

signals:

    /// Fired when we're simulating; allows external parties to hook in.
    void simulating(float deltaTime);

    /// Fired when we're rendering in-world interface elements; allows external parties to hook in.
    void renderingInWorldInterface();

    /// Fired when we're rendering the overlay.
    void renderingOverlay();

    /// Fired when the import window is closed
    void importDone();

public slots:
    void domainChanged(const QString& domainHostname);
    void updateWindowTitle();
    void updateLocationInServer();
    void nodeAdded(SharedNodePointer node);
    void nodeKilled(SharedNodePointer node);
    void packetSent(quint64 length);

    void importVoxels(); // doesn't include source voxel because it goes to clipboard
    void cutVoxels(const VoxelDetail& sourceVoxel);
    void copyVoxels(const VoxelDetail& sourceVoxel);
    void pasteVoxels(const VoxelDetail& sourceVoxel);
    void deleteVoxels(const VoxelDetail& sourceVoxel);
    void exportVoxels(const VoxelDetail& sourceVoxel);
    void nudgeVoxelsByVector(const VoxelDetail& sourceVoxel, const glm::vec3& nudgeVec);

    void setRenderVoxels(bool renderVoxels);
    void doKillLocalVoxels();
    void loadDialog();
    void loadScriptURLDialog();
    void toggleLogDialog();
    void initAvatarAndViewFrustum();
    ScriptEngine* loadScript(const QString& fileNameString, bool loadScriptFromEditor = false);
    void scriptFinished(const QString& scriptName);
    void stopAllScripts(bool restart = false);
    void stopScript(const QString& scriptName);
    void reloadAllScripts();
    void toggleRunningScriptsWidget();

    void uploadHead();
    void uploadSkeleton();
    void uploadAttachment();

    void bumpSettings() { ++_numChangedSettings; }

private slots:
    void timer();
    void idle();

    void connectedToDomain(const QString& hostname);

    void setFullscreen(bool fullscreen);
    void setEnable3DTVMode(bool enable3DTVMode);
    void cameraMenuChanged();

    glm::vec2 getScaledScreenPoint(glm::vec2 projectedPoint);

    void closeMirrorView();
    void restoreMirrorView();
    void shrinkMirrorView();
    void resetSensors();

    void parseVersionXml();

    void manageRunningScriptsWidgetVisibility(bool shown);

private:
    void resetCamerasOnResizeGL(Camera& camera, int width, int height);
    void updateProjectionMatrix();
    void updateProjectionMatrix(Camera& camera, bool updateViewFrustum = true);

    static bool sendVoxelsOperation(OctreeElement* node, void* extraData);
    void sendPingPackets();

    void initDisplay();
    void init();

    void update(float deltaTime);

    // Various helper functions called during update()
    void updateLOD();
    void updateMouseRay();
    void updateFaceplus();
    void updateFaceshift();
    void updateVisage();
    void updateMyAvatarLookAtPosition();
    void updateThreads(float deltaTime);
    void updateMetavoxels(float deltaTime);
    void updateCamera(float deltaTime);
    void updateDialogs(float deltaTime);
    void updateCursor(float deltaTime);

    Avatar* findLookatTargetAvatar(glm::vec3& eyePosition, QUuid &nodeUUID);
    bool isLookingAtMyAvatar(Avatar* avatar);

    void renderLookatIndicator(glm::vec3 pointOfInterest);

    void updateMyAvatar(float deltaTime);
    void queryOctree(NodeType_t serverType, PacketType packetType, NodeToJurisdictionMap& jurisdictions);
    void loadViewFrustum(Camera& camera, ViewFrustum& viewFrustum);

    glm::vec3 getSunDirection();

    void updateShadowMap();
    void renderRearViewMirror(const QRect& region, bool billboard = false);
    void renderViewFrustum(ViewFrustum& viewFrustum);

    void checkBandwidthMeterClick();

    void deleteVoxelAt(const VoxelDetail& voxel);
    void eyedropperVoxelUnderCursor();

    void setMenuShortcutsEnabled(bool enabled);

    void uploadModel(ModelType modelType);

    static void attachNewHeadToNode(Node *newNode);
    static void* networkReceive(void* args); // network receive thread

    MainWindow* _window;
    GLCanvas* _glWidget; // our GLCanvas has a couple extra features

    BandwidthMeter _bandwidthMeter;

    QThread* _nodeThread;
    DatagramProcessor _datagramProcessor;

    QNetworkAccessManager* _networkAccessManager;
    QMutex _settingsMutex;
    QSettings* _settings;
    int _numChangedSettings;

    QUndoStack _undoStack;

    glm::vec3 _gravity;

    // Frame Rate Measurement

    int _frameCount;
    float _fps;
    QElapsedTimer _applicationStartupTime;
    QElapsedTimer _timerStart;
    QElapsedTimer _lastTimeUpdated;
    bool _justStarted;
    Stars _stars;

    BuckyBalls _buckyBalls;

    VoxelSystem _voxels;
    VoxelTree _clipboard; // if I copy/paste
    VoxelImporter* _voxelImporter;
    bool _importSucceded;
    VoxelSystem _sharedVoxelSystem;
    ViewFrustum _sharedVoxelSystemViewFrustum;

    ParticleTreeRenderer _particles;
    ParticleCollisionSystem _particleCollisionSystem;

    ModelTreeRenderer _models;

    QByteArray _voxelsFilename;
    bool _wantToKillLocalVoxels;

    MetavoxelSystem _metavoxels;

    ViewFrustum _viewFrustum; // current state of view frustum, perspective, orientation, etc.
    ViewFrustum _lastQueriedViewFrustum; /// last view frustum used to query octree servers (voxels, particles)
    ViewFrustum _shadowViewFrustum;
    quint64 _lastQueriedTime;

    float _trailingAudioLoudness;

    OctreeQuery _octreeQuery; // NodeData derived class for querying voxels from voxel server

    AvatarManager _avatarManager;
    MyAvatar* _myAvatar;            // TODO: move this and relevant code to AvatarManager (or MyAvatar as the case may be)

    Faceplus _faceplus;
    Faceshift _faceshift;
    Visage _visage;

    SixenseManager _sixenseManager;
    PrioVR _prioVR;
    JoystickManager _joystickManager;

    Camera _myCamera;                  // My view onto the world
    Camera _viewFrustumOffsetCamera;   // The camera we use to sometimes show the view frustum from an offset mode
    Camera _mirrorCamera;              // Cammera for mirror view
    QRect _mirrorViewRect;
    RearMirrorTools* _rearMirrorTools;

    float _cameraPushback;
    glm::mat4 _untranslatedViewMatrix;
    glm::vec3 _viewMatrixTranslation;
    glm::mat4 _projectionMatrix;

    float _scaleMirror;
    float _rotateMirror;
    float _raiseMirror;

    glm::mat4 _shadowMatrix;

    Environment _environment;

    int _mouseX;
    int _mouseY;
    int _mouseDragStartedX;
    int _mouseDragStartedY;
    quint64 _lastMouseMove;
    bool _mouseHidden;
    bool _seenMouseMove;

    glm::vec3 _mouseRayOrigin;
    glm::vec3 _mouseRayDirection;

    float _touchAvgX;
    float _touchAvgY;
    float _touchDragStartedAvgX;
    float _touchDragStartedAvgY;
    bool _isTouchPressed; //  true if multitouch has been pressed (clear when finished)

    bool _mousePressed; //  true if mouse has been pressed (clear when finished)

    QSet<int> _keysPressed;

    GeometryCache _geometryCache;
    AnimationCache _animationCache;
    TextureCache _textureCache;

    GlowEffect _glowEffect;
    AmbientOcclusionEffect _ambientOcclusionEffect;
    VoxelShader _voxelShader;
    PointShader _pointShader;

    Audio _audio;

    bool _enableProcessVoxelsThread;
    VoxelPacketProcessor _voxelProcessor;
    VoxelHideShowThread _voxelHideShowThread;
    VoxelEditPacketSender _voxelEditSender;
    ParticleEditPacketSender _particleEditSender;
    ModelEditPacketSender _modelEditSender;

    int _packetsPerSecond;
    int _bytesPerSecond;

    StDev _idleLoopStdev;
    float _idleLoopMeasuredJitter;

    int parseOctreeStats(const QByteArray& packet, const SharedNodePointer& sendingNode);
    void trackIncomingVoxelPacket(const QByteArray& packet, const SharedNodePointer& sendingNode, bool wasStatsPacket);

    NodeToJurisdictionMap _voxelServerJurisdictions;
    NodeToJurisdictionMap _particleServerJurisdictions;
    NodeToJurisdictionMap _modelServerJurisdictions;
    NodeToOctreeSceneStats _octreeServerSceneStats;
    QReadWriteLock _octreeSceneStatsLock;

    NodeBounds _nodeBoundsDisplay;

    std::vector<VoxelFade> _voxelFades;
    QReadWriteLock _voxelFadesLock;
    ControllerScriptingInterface _controllerScriptingInterface;
    QPointer<LogDialog> _logDialog;
    QPointer<SnapshotShareDialog> _snapshotShareDialog;

    QString _previousScriptLocation;

    FileLogger* _logger;

    void checkVersion();
    void displayUpdateDialog();
    bool shouldSkipVersion(QString latestVersion);
    void takeSnapshot();

    TouchEvent _lastTouchEvent;

    Overlays _overlays;
    ApplicationOverlay _applicationOverlay;

    AudioReflector _audioReflector;
    RunningScriptsWidget* _runningScriptsWidget;
    QHash<QString, ScriptEngine*> _scriptEnginesHash;
    bool _runningScriptsWidgetWasVisible;

    QSystemTrayIcon* _trayIcon;
};

#endif // hifi_Application_h
