//
//  Menu.cpp
//  interface/src
//
//  Created by Stephen Birarda on 8/12/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <cstdlib>


#include <QBoxLayout>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QSlider>
#include <QUuid>
#include <QHBoxLayout>
#include <QDesktopServices>

#include <AccountManager.h>
#include <XmppClient.h>
#include <UUID.h>

#include "Application.h"
#include "AccountManager.h"
#include "Menu.h"
#include "scripting/MenuScriptingInterface.h"
#include "Util.h"
#include "ui/AnimationsDialog.h"
#include "ui/AttachmentsDialog.h"
#include "ui/InfoView.h"
#include "ui/MetavoxelEditor.h"
#include "ui/ModelsBrowser.h"
#include "ui/LoginDialog.h"
#include "ui/NodeBounds.h"


Menu* Menu::_instance = NULL;

Menu* Menu::getInstance() {
    static QMutex menuInstanceMutex;

    // lock the menu instance mutex to make sure we don't race and create two menus and crash
    menuInstanceMutex.lock();

    if (!_instance) {
        qDebug("First call to Menu::getInstance() - initing menu.");

        _instance = new Menu();
    }

    menuInstanceMutex.unlock();

    return _instance;
}

const ViewFrustumOffset DEFAULT_FRUSTUM_OFFSET = {-135.0f, 0.0f, 0.0f, 25.0f, 0.0f};
const float DEFAULT_FACESHIFT_EYE_DEFLECTION = 0.25f;
const float DEFAULT_AVATAR_LOD_DISTANCE_MULTIPLIER = 1.0f;
const int ONE_SECOND_OF_FRAMES = 60;
const int FIVE_SECONDS_OF_FRAMES = 5 * ONE_SECOND_OF_FRAMES;
const float MUTE_RADIUS = 50;

Menu::Menu() :
    _actionHash(),
    _audioJitterBufferSamples(0),
    _bandwidthDialog(NULL),
    _fieldOfView(DEFAULT_FIELD_OF_VIEW_DEGREES),
    _faceshiftEyeDeflection(DEFAULT_FACESHIFT_EYE_DEFLECTION),
    _frustumDrawMode(FRUSTUM_DRAW_MODE_ALL),
    _viewFrustumOffset(DEFAULT_FRUSTUM_OFFSET),
    _octreeStatsDialog(NULL),
    _lodToolsDialog(NULL),
    _maxVoxels(DEFAULT_MAX_VOXELS_PER_SYSTEM),
    _voxelSizeScale(DEFAULT_OCTREE_SIZE_SCALE),
    _automaticAvatarLOD(true),
    _avatarLODDecreaseFPS(DEFAULT_ADJUST_AVATAR_LOD_DOWN_FPS),
    _avatarLODIncreaseFPS(ADJUST_LOD_UP_FPS),
    _avatarLODDistanceMultiplier(DEFAULT_AVATAR_LOD_DISTANCE_MULTIPLIER),
    _boundaryLevelAdjust(0),
    _maxVoxelPacketsPerSecond(DEFAULT_MAX_VOXEL_PPS),
    _lastAdjust(usecTimestampNow()),
    _lastAvatarDetailDrop(usecTimestampNow()),
    _fpsAverage(FIVE_SECONDS_OF_FRAMES),
    _fastFPSAverage(ONE_SECOND_OF_FRAMES),
    _loginAction(NULL),
    _preferencesDialog(NULL),
    _snapshotsLocation()
{
    Application *appInstance = Application::getInstance();

    QMenu* fileMenu = addMenu("File");

#ifdef Q_OS_MAC
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::AboutApp,
                                  0,
                                  this,
                                  SLOT(aboutApp()),
                                  QAction::AboutRole);
#endif

    AccountManager& accountManager = AccountManager::getInstance();

    _loginAction = addActionToQMenuAndActionHash(fileMenu, MenuOption::Logout);

    // call our toggle login function now so the menu option is setup properly
    toggleLoginMenuItem();

    // connect to the appropriate slots of the AccountManager so that we can change the Login/Logout menu item
    connect(&accountManager, &AccountManager::accessTokenChanged, this, &Menu::toggleLoginMenuItem);
    connect(&accountManager, &AccountManager::logoutComplete, this, &Menu::toggleLoginMenuItem);

    addDisabledActionAndSeparator(fileMenu, "Scripts");
    addActionToQMenuAndActionHash(fileMenu, MenuOption::LoadScript, Qt::CTRL | Qt::Key_O, appInstance, SLOT(loadDialog()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::LoadScriptURL,
                                    Qt::CTRL | Qt::SHIFT | Qt::Key_O, appInstance, SLOT(loadScriptURLDialog()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::StopAllScripts, 0, appInstance, SLOT(stopAllScripts()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::ReloadAllScripts, 0, appInstance, SLOT(reloadAllScripts()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::RunningScripts, Qt::CTRL | Qt::Key_J,
                                  appInstance, SLOT(toggleRunningScriptsWidget()));

    addDisabledActionAndSeparator(fileMenu, "Go");
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::GoHome,
                                  Qt::CTRL | Qt::Key_G,
                                  appInstance->getAvatar(),
                                  SLOT(goHome()));
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::GoToDomain,
                                  Qt::CTRL | Qt::Key_D,
                                  this,
                                  SLOT(goToDomainDialog()));
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::GoToLocation,
                                  Qt::CTRL | Qt::SHIFT | Qt::Key_L,
                                  this,
                                  SLOT(goToLocation()));
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::NameLocation,
                                  Qt::CTRL | Qt::Key_N,
                                  this,
                                  SLOT(nameLocation()));
    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::GoTo,
                                  Qt::Key_At,
                                  this,
                                  SLOT(goTo()));

    addDisabledActionAndSeparator(fileMenu, "Upload Avatar Model");
    addActionToQMenuAndActionHash(fileMenu, MenuOption::UploadHead, 0, Application::getInstance(), SLOT(uploadHead()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::UploadSkeleton, 0, Application::getInstance(), SLOT(uploadSkeleton()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::UploadAttachment, 0,
        Application::getInstance(), SLOT(uploadAttachment()));
    addDisabledActionAndSeparator(fileMenu, "Settings");
    addActionToQMenuAndActionHash(fileMenu, MenuOption::SettingsImport, 0, this, SLOT(importSettings()));
    addActionToQMenuAndActionHash(fileMenu, MenuOption::SettingsExport, 0, this, SLOT(exportSettings()));

    addActionToQMenuAndActionHash(fileMenu,
                                  MenuOption::Quit,
                                  Qt::CTRL | Qt::Key_Q,
                                  appInstance,
                                  SLOT(quit()),
                                  QAction::QuitRole);


    QMenu* editMenu = addMenu("Edit");

    QUndoStack* undoStack = Application::getInstance()->getUndoStack();
    QAction* undoAction = undoStack->createUndoAction(editMenu);
    undoAction->setShortcut(Qt::CTRL | Qt::Key_Z);
    addActionToQMenuAndActionHash(editMenu, undoAction);

    QAction* redoAction = undoStack->createRedoAction(editMenu);
    redoAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Z);
    addActionToQMenuAndActionHash(editMenu, redoAction);

    addActionToQMenuAndActionHash(editMenu,
                                  MenuOption::Preferences,
                                  Qt::CTRL | Qt::Key_Comma,
                                  this,
                                  SLOT(editPreferences()),
                                  QAction::PreferencesRole);

    addActionToQMenuAndActionHash(editMenu, MenuOption::Attachments, 0, this, SLOT(editAttachments()));
    addActionToQMenuAndActionHash(editMenu, MenuOption::Animations, 0, this, SLOT(editAnimations()));

    addDisabledActionAndSeparator(editMenu, "Physics");
    QObject* avatar = appInstance->getAvatar();
    addCheckableActionToQMenuAndActionHash(editMenu, MenuOption::ObeyEnvironmentalGravity, Qt::SHIFT | Qt::Key_G, false,
            avatar, SLOT(updateMotionBehaviorsFromMenu()));
    addCheckableActionToQMenuAndActionHash(editMenu, MenuOption::StandOnNearbyFloors, 0, true,
            avatar, SLOT(updateMotionBehaviorsFromMenu()));

    addAvatarCollisionSubMenu(editMenu);

    QMenu* toolsMenu = addMenu("Tools");
    addActionToQMenuAndActionHash(toolsMenu, MenuOption::MetavoxelEditor, 0, this, SLOT(showMetavoxelEditor()));
    addActionToQMenuAndActionHash(toolsMenu, MenuOption::ScriptEditor,  Qt::ALT | Qt::Key_S, this, SLOT(showScriptEditor()));

#ifdef HAVE_QXMPP
    _chatAction = addActionToQMenuAndActionHash(toolsMenu,
                                                MenuOption::Chat,
                                                0,
                                                this,
                                                SLOT(showChat()));

    const QXmppClient& xmppClient = XmppClient::getInstance().getXMPPClient();
    toggleChat();
    connect(&xmppClient, SIGNAL(connected()), this, SLOT(toggleChat()));
    connect(&xmppClient, SIGNAL(disconnected()), this, SLOT(toggleChat()));

    QDir::setCurrent(Application::resourcesPath());
    // init chat window to listen chat
    _chatWindow = new ChatWindow(Application::getInstance()->getWindow());
#endif

    QMenu* viewMenu = addMenu("View");

    addCheckableActionToQMenuAndActionHash(viewMenu,
                                           MenuOption::Fullscreen,
                                           Qt::CTRL | Qt::META | Qt::Key_F,
                                           false,
                                           appInstance,
                                           SLOT(setFullscreen(bool)));
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::FirstPerson, Qt::Key_P, true,
                                            appInstance,SLOT(cameraMenuChanged()));
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Mirror, Qt::SHIFT | Qt::Key_H, true);
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::FullscreenMirror, Qt::Key_H, false,
                                            appInstance, SLOT(cameraMenuChanged()));

    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Enable3DTVMode, 0,
                                           false,
                                           appInstance,
                                           SLOT(setEnable3DTVMode(bool)));


    QMenu* nodeBordersMenu = viewMenu->addMenu("Server Borders");
    NodeBounds& nodeBounds = appInstance->getNodeBoundsDisplay();
    addCheckableActionToQMenuAndActionHash(nodeBordersMenu, MenuOption::ShowBordersVoxelNodes,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_1, false,
                                           &nodeBounds, SLOT(setShowVoxelNodes(bool)));
    addCheckableActionToQMenuAndActionHash(nodeBordersMenu, MenuOption::ShowBordersModelNodes,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_2, false,
                                           &nodeBounds, SLOT(setShowModelNodes(bool)));
    addCheckableActionToQMenuAndActionHash(nodeBordersMenu, MenuOption::ShowBordersParticleNodes,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_3, false,
                                           &nodeBounds, SLOT(setShowParticleNodes(bool)));


    QMenu* avatarSizeMenu = viewMenu->addMenu("Avatar Size");

    addActionToQMenuAndActionHash(avatarSizeMenu,
                                  MenuOption::IncreaseAvatarSize,
                                  Qt::Key_Plus,
                                  appInstance->getAvatar(),
                                  SLOT(increaseSize()));
    addActionToQMenuAndActionHash(avatarSizeMenu,
                                  MenuOption::DecreaseAvatarSize,
                                  Qt::Key_Minus,
                                  appInstance->getAvatar(),
                                  SLOT(decreaseSize()));
    addActionToQMenuAndActionHash(avatarSizeMenu,
                                  MenuOption::ResetAvatarSize,
                                  Qt::Key_Equal,
                                  appInstance->getAvatar(),
                                  SLOT(resetSize()));

    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::OffAxisProjection, 0, false);
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::TurnWithHead, 0, false);
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::MoveWithLean, 0, false);
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::HeadMouse, 0, false);


    addDisabledActionAndSeparator(viewMenu, "Stats");
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Stats, Qt::Key_Slash);
    addActionToQMenuAndActionHash(viewMenu, MenuOption::Log, Qt::CTRL | Qt::Key_L, appInstance, SLOT(toggleLogDialog()));
    addCheckableActionToQMenuAndActionHash(viewMenu, MenuOption::Bandwidth, 0, true);
    addActionToQMenuAndActionHash(viewMenu, MenuOption::BandwidthDetails, 0, this, SLOT(bandwidthDetails()));
    addActionToQMenuAndActionHash(viewMenu, MenuOption::OctreeStats, 0, this, SLOT(octreeStatsDetails()));

    QMenu* developerMenu = addMenu("Developer");

    QMenu* renderOptionsMenu = developerMenu->addMenu("Rendering Options");

    addCheckableActionToQMenuAndActionHash(renderOptionsMenu, MenuOption::Stars, Qt::Key_Asterisk, true);
    addCheckableActionToQMenuAndActionHash(renderOptionsMenu, MenuOption::Atmosphere, Qt::SHIFT | Qt::Key_A, true);
    addActionToQMenuAndActionHash(renderOptionsMenu,
                                  MenuOption::GlowMode,
                                  0,
                                  appInstance->getGlowEffect(),
                                  SLOT(cycleRenderMode()));

    addCheckableActionToQMenuAndActionHash(renderOptionsMenu, MenuOption::Shadows, 0, false);
    addCheckableActionToQMenuAndActionHash(renderOptionsMenu, MenuOption::Metavoxels, 0, true);
    addCheckableActionToQMenuAndActionHash(renderOptionsMenu, MenuOption::BuckyBalls, 0, false);
    addCheckableActionToQMenuAndActionHash(renderOptionsMenu, MenuOption::Particles, 0, true);
    addActionToQMenuAndActionHash(renderOptionsMenu, MenuOption::LodTools, Qt::SHIFT | Qt::Key_L, this, SLOT(lodTools()));

    QMenu* voxelOptionsMenu = developerMenu->addMenu("Voxel Options");

    addCheckableActionToQMenuAndActionHash(voxelOptionsMenu,
                                           MenuOption::Voxels,
                                           Qt::SHIFT | Qt::Key_V,
                                           true,
                                           appInstance,
                                           SLOT(setRenderVoxels(bool)));

    addCheckableActionToQMenuAndActionHash(voxelOptionsMenu, MenuOption::VoxelTextures);
    addCheckableActionToQMenuAndActionHash(voxelOptionsMenu, MenuOption::AmbientOcclusion);
    addCheckableActionToQMenuAndActionHash(voxelOptionsMenu, MenuOption::DontFadeOnVoxelServerChanges);
    addCheckableActionToQMenuAndActionHash(voxelOptionsMenu, MenuOption::DisableAutoAdjustLOD);

    QMenu* modelOptionsMenu = developerMenu->addMenu("Model Options");
    addCheckableActionToQMenuAndActionHash(modelOptionsMenu, MenuOption::Models, 0, true);
    addCheckableActionToQMenuAndActionHash(modelOptionsMenu, MenuOption::DisplayModelBounds, 0, false);
    addCheckableActionToQMenuAndActionHash(modelOptionsMenu, MenuOption::DisplayModelElementProxy, 0, false);
    addCheckableActionToQMenuAndActionHash(modelOptionsMenu, MenuOption::DisplayModelElementChildProxies, 0, false);

    QMenu* avatarOptionsMenu = developerMenu->addMenu("Avatar Options");

    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::AllowOculusCameraModeChange, 0, false);
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::Avatars, 0, true);
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::AvatarsReceiveShadows, 0, true);
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::RenderSkeletonCollisionShapes);
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::RenderHeadCollisionShapes);
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::RenderBoundingCollisionShapes);

    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::LookAtVectors, 0, false);
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu,
                                           MenuOption::Faceshift,
                                           0,
                                           true,
                                           appInstance->getFaceshift(),
                                           SLOT(setTCPEnabled(bool)));
#ifdef HAVE_FACEPLUS
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::Faceplus, 0, true,
        appInstance->getFaceplus(), SLOT(updateEnabled()));
#endif

#ifdef HAVE_VISAGE
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::Visage, 0, false,
        appInstance->getVisage(), SLOT(updateEnabled()));
#endif

    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::GlowWhenSpeaking, 0, true);
    addCheckableActionToQMenuAndActionHash(avatarOptionsMenu, MenuOption::ChatCircling, 0, false);

    QMenu* handOptionsMenu = developerMenu->addMenu("Hand Options");

    addCheckableActionToQMenuAndActionHash(handOptionsMenu,
                                           MenuOption::FilterSixense,
                                           0,
                                           true,
                                           appInstance->getSixenseManager(),
                                           SLOT(setFilter(bool)));
    addCheckableActionToQMenuAndActionHash(handOptionsMenu, MenuOption::DisplayHands, 0, true);
    addCheckableActionToQMenuAndActionHash(handOptionsMenu, MenuOption::DisplayHandTargets, 0, false);
    addCheckableActionToQMenuAndActionHash(handOptionsMenu, MenuOption::HandsCollideWithSelf, 0, false);
    addCheckableActionToQMenuAndActionHash(handOptionsMenu, MenuOption::ShowIKConstraints, 0, false);
    addCheckableActionToQMenuAndActionHash(handOptionsMenu, MenuOption::AlignForearmsWithWrists, 0, true);
    addCheckableActionToQMenuAndActionHash(handOptionsMenu, MenuOption::AlternateIK, 0, false);

    addDisabledActionAndSeparator(developerMenu, "Testing");

    QMenu* timingMenu = developerMenu->addMenu("Timing and Statistics Tools");
    addCheckableActionToQMenuAndActionHash(timingMenu, MenuOption::TestPing, 0, true);
    addCheckableActionToQMenuAndActionHash(timingMenu, MenuOption::FrameTimer);
    addActionToQMenuAndActionHash(timingMenu, MenuOption::RunTimingTests, 0, this, SLOT(runTests()));

    QMenu* frustumMenu = developerMenu->addMenu("View Frustum Debugging Tools");
    addCheckableActionToQMenuAndActionHash(frustumMenu, MenuOption::DisplayFrustum, Qt::SHIFT | Qt::Key_F);
    addActionToQMenuAndActionHash(frustumMenu,
                                  MenuOption::FrustumRenderMode,
                                  Qt::SHIFT | Qt::Key_R,
                                  this,
                                  SLOT(cycleFrustumRenderMode()));
    updateFrustumRenderModeAction();


    QMenu* renderDebugMenu = developerMenu->addMenu("Render Debugging Tools");
    addCheckableActionToQMenuAndActionHash(renderDebugMenu, MenuOption::PipelineWarnings);
    addCheckableActionToQMenuAndActionHash(renderDebugMenu, MenuOption::SuppressShortTimings);

    QMenu* audioDebugMenu = developerMenu->addMenu("Audio Debugging Tools");
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::AudioNoiseReduction,
                                           0,
                                           true,
                                           appInstance->getAudio(),
                                           SLOT(toggleAudioNoiseReduction()));
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::EchoServerAudio);
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::EchoLocalAudio);
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::MuteAudio,
                                           Qt::CTRL | Qt::Key_M,
                                           false,
                                           appInstance->getAudio(),
                                           SLOT(toggleMute()));
    addActionToQMenuAndActionHash(audioDebugMenu,
                                  MenuOption::MuteEnvironment,
                                  0,
                                  this,
                                  SLOT(muteEnvironment()));
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::AudioToneInjection,
                                           0,
                                           false,
                                           appInstance->getAudio(),
                                           SLOT(toggleToneInjection()));
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::AudioScope, 
                                           Qt::CTRL | Qt::Key_P, false,
                                           appInstance->getAudio(),
                                           SLOT(toggleScope()));
    addCheckableActionToQMenuAndActionHash(audioDebugMenu, MenuOption::AudioScopePause,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_P ,
                                           false,
                                           appInstance->getAudio(),
                                           SLOT(toggleScopePause()));

    QMenu* audioScopeMenu = audioDebugMenu->addMenu("Audio Scope Options");
    addDisabledActionAndSeparator(audioScopeMenu, "Display Frames");
    {
        QAction *fiveFrames = addCheckableActionToQMenuAndActionHash(audioScopeMenu, MenuOption::AudioScopeFiveFrames,
                                               0,
                                               true,
                                               appInstance->getAudio(),
                                               SLOT(selectAudioScopeFiveFrames()));

        QAction *twentyFrames = addCheckableActionToQMenuAndActionHash(audioScopeMenu, MenuOption::AudioScopeTwentyFrames,
                                               0,
                                               false,
                                               appInstance->getAudio(),
                                               SLOT(selectAudioScopeTwentyFrames()));

        QAction *fiftyFrames = addCheckableActionToQMenuAndActionHash(audioScopeMenu, MenuOption::AudioScopeFiftyFrames,
                                               0,
                                               false,
                                               appInstance->getAudio(),
                                               SLOT(selectAudioScopeFiftyFrames()));

        QActionGroup* audioScopeFramesGroup = new QActionGroup(audioScopeMenu);
        audioScopeFramesGroup->addAction(fiveFrames);
        audioScopeFramesGroup->addAction(twentyFrames);
        audioScopeFramesGroup->addAction(fiftyFrames);
    }

    QMenu* spatialAudioMenu = audioDebugMenu->addMenu("Spatial Audio");

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessing,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_M,
                                           false,
                                           appInstance->getAudio(),
                                           SLOT(toggleAudioSpatialProcessing()));

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingIncludeOriginal,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_O,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingSeparateEars,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_E,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingPreDelay,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_D,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingStereoSource,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_S,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingHeadOriented,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_H,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingWithDiffusions,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_W,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingRenderPaths,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_R,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingSlightlyRandomSurfaces,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_X,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingProcessLocalAudio,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_A,
                                           true);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingDontDistanceAttenuate,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_Y,
                                           false);

    addCheckableActionToQMenuAndActionHash(spatialAudioMenu, MenuOption::AudioSpatialProcessingAlternateDistanceAttenuate,
                                           Qt::CTRL | Qt::SHIFT | Qt::Key_U,
                                           false);

    addActionToQMenuAndActionHash(developerMenu, MenuOption::PasteToVoxel,
                Qt::CTRL | Qt::SHIFT | Qt::Key_V,
                this,
                SLOT(pasteToVoxel()));

    connect(appInstance->getAudio(), SIGNAL(muteToggled()), this, SLOT(audioMuteToggled()));

#ifndef Q_OS_MAC
    QMenu* helpMenu = addMenu("Help");
    QAction* helpAction = helpMenu->addAction(MenuOption::AboutApp);
    connect(helpAction, SIGNAL(triggered()), this, SLOT(aboutApp()));
#endif
}

Menu::~Menu() {
    bandwidthDetailsClosed();
    octreeStatsDetailsClosed();
}

void Menu::loadSettings(QSettings* settings) {
    bool lockedSettings = false;
    if (!settings) {
        settings = Application::getInstance()->lockSettings();
        lockedSettings = true;
    }

    _audioJitterBufferSamples = loadSetting(settings, "audioJitterBufferSamples", 0);
    _fieldOfView = loadSetting(settings, "fieldOfView", DEFAULT_FIELD_OF_VIEW_DEGREES);
    _realWorldFieldOfView = loadSetting(settings, "realWorldFieldOfView", DEFAULT_REAL_WORLD_FIELD_OF_VIEW_DEGREES);
    _faceshiftEyeDeflection = loadSetting(settings, "faceshiftEyeDeflection", DEFAULT_FACESHIFT_EYE_DEFLECTION);
    _maxVoxels = loadSetting(settings, "maxVoxels", DEFAULT_MAX_VOXELS_PER_SYSTEM);
    _maxVoxelPacketsPerSecond = loadSetting(settings, "maxVoxelsPPS", DEFAULT_MAX_VOXEL_PPS);
    _voxelSizeScale = loadSetting(settings, "voxelSizeScale", DEFAULT_OCTREE_SIZE_SCALE);
    _automaticAvatarLOD = settings->value("automaticAvatarLOD", true).toBool();
    _avatarLODDecreaseFPS = loadSetting(settings, "avatarLODDecreaseFPS", DEFAULT_ADJUST_AVATAR_LOD_DOWN_FPS);
    _avatarLODIncreaseFPS = loadSetting(settings, "avatarLODIncreaseFPS", ADJUST_LOD_UP_FPS);
    _avatarLODDistanceMultiplier = loadSetting(settings, "avatarLODDistanceMultiplier",
        DEFAULT_AVATAR_LOD_DISTANCE_MULTIPLIER);
    _boundaryLevelAdjust = loadSetting(settings, "boundaryLevelAdjust", 0);
    _snapshotsLocation = settings->value("snapshotsLocation",
                                         QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)).toString();

    settings->beginGroup("View Frustum Offset Camera");
    // in case settings is corrupt or missing loadSetting() will check for NaN
    _viewFrustumOffset.yaw = loadSetting(settings, "viewFrustumOffsetYaw", 0.0f);
    _viewFrustumOffset.pitch = loadSetting(settings, "viewFrustumOffsetPitch", 0.0f);
    _viewFrustumOffset.roll = loadSetting(settings, "viewFrustumOffsetRoll", 0.0f);
    _viewFrustumOffset.distance = loadSetting(settings, "viewFrustumOffsetDistance", 0.0f);
    _viewFrustumOffset.up = loadSetting(settings, "viewFrustumOffsetUp", 0.0f);
    settings->endGroup();

    scanMenuBar(&loadAction, settings);
    Application::getInstance()->getAvatar()->loadData(settings);
    Application::getInstance()->updateWindowTitle();
    NodeList::getInstance()->loadData(settings);

    // MyAvatar caches some menu options, so we have to update them whenever we load settings.
    // TODO: cache more settings in MyAvatar that are checked with very high frequency.
    MyAvatar* myAvatar = Application::getInstance()->getAvatar();
    myAvatar->updateCollisionGroups();

    if (lockedSettings) {
        Application::getInstance()->unlockSettings();
    }
}

void Menu::saveSettings(QSettings* settings) {
    bool lockedSettings = false;
    if (!settings) {
        settings = Application::getInstance()->lockSettings();
        lockedSettings = true;
    }

    settings->setValue("audioJitterBufferSamples", _audioJitterBufferSamples);
    settings->setValue("fieldOfView", _fieldOfView);
    settings->setValue("faceshiftEyeDeflection", _faceshiftEyeDeflection);
    settings->setValue("maxVoxels", _maxVoxels);
    settings->setValue("maxVoxelsPPS", _maxVoxelPacketsPerSecond);
    settings->setValue("voxelSizeScale", _voxelSizeScale);
    settings->setValue("automaticAvatarLOD", _automaticAvatarLOD);
    settings->setValue("avatarLODDecreaseFPS", _avatarLODDecreaseFPS);
    settings->setValue("avatarLODIncreaseFPS", _avatarLODIncreaseFPS);
    settings->setValue("avatarLODDistanceMultiplier", _avatarLODDistanceMultiplier);
    settings->setValue("boundaryLevelAdjust", _boundaryLevelAdjust);
    settings->setValue("snapshotsLocation", _snapshotsLocation);
    settings->beginGroup("View Frustum Offset Camera");
    settings->setValue("viewFrustumOffsetYaw", _viewFrustumOffset.yaw);
    settings->setValue("viewFrustumOffsetPitch", _viewFrustumOffset.pitch);
    settings->setValue("viewFrustumOffsetRoll", _viewFrustumOffset.roll);
    settings->setValue("viewFrustumOffsetDistance", _viewFrustumOffset.distance);
    settings->setValue("viewFrustumOffsetUp", _viewFrustumOffset.up);
    settings->endGroup();

    scanMenuBar(&saveAction, settings);
    Application::getInstance()->getAvatar()->saveData(settings);
    NodeList::getInstance()->saveData(settings);

    if (lockedSettings) {
        Application::getInstance()->unlockSettings();
    }
}

void Menu::importSettings() {
    QString locationDir(QStandardPaths::displayName(QStandardPaths::DesktopLocation));
    QString fileName = QFileDialog::getOpenFileName(Application::getInstance()->getWindow(),
                                                    tr("Open .ini config file"),
                                                    locationDir,
                                                    tr("Text files (*.ini)"));
    if (fileName != "") {
        QSettings tmp(fileName, QSettings::IniFormat);
        loadSettings(&tmp);
    }
}

void Menu::exportSettings() {
    QString locationDir(QStandardPaths::displayName(QStandardPaths::DesktopLocation));
    QString fileName = QFileDialog::getSaveFileName(Application::getInstance()->getWindow(),
                                                    tr("Save .ini config file"),
                                                    locationDir,
                                                    tr("Text files (*.ini)"));
    if (fileName != "") {
        QSettings tmp(fileName, QSettings::IniFormat);
        saveSettings(&tmp);
        tmp.sync();
    }
}

void Menu::loadAction(QSettings* set, QAction* action) {
    if (action->isChecked() != set->value(action->text(), action->isChecked()).toBool()) {
        action->trigger();
    }
}

void Menu::saveAction(QSettings* set, QAction* action) {
    set->setValue(action->text(),  action->isChecked());
}

void Menu::scanMenuBar(settingsAction modifySetting, QSettings* set) {
    QList<QMenu*> menus = this->findChildren<QMenu *>();

    for (QList<QMenu *>::const_iterator it = menus.begin(); menus.end() != it; ++it) {
        scanMenu(*it, modifySetting, set);
    }
}

void Menu::scanMenu(QMenu* menu, settingsAction modifySetting, QSettings* set) {
    QList<QAction*> actions = menu->actions();

    set->beginGroup(menu->title());
    for (QList<QAction *>::const_iterator it = actions.begin(); actions.end() != it; ++it) {
        if ((*it)->menu()) {
            scanMenu((*it)->menu(), modifySetting, set);
        }
        if ((*it)->isCheckable()) {
            modifySetting(set, *it);
        }
    }
    set->endGroup();
}

void Menu::handleViewFrustumOffsetKeyModifier(int key) {
    const float VIEW_FRUSTUM_OFFSET_DELTA = 0.5f;
    const float VIEW_FRUSTUM_OFFSET_UP_DELTA = 0.05f;

    switch (key) {
        case Qt::Key_BracketLeft:
            _viewFrustumOffset.yaw -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_BracketRight:
            _viewFrustumOffset.yaw += VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_BraceLeft:
            _viewFrustumOffset.pitch -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_BraceRight:
            _viewFrustumOffset.pitch += VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_ParenLeft:
            _viewFrustumOffset.roll -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_ParenRight:
            _viewFrustumOffset.roll += VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_Less:
            _viewFrustumOffset.distance -= VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_Greater:
            _viewFrustumOffset.distance += VIEW_FRUSTUM_OFFSET_DELTA;
            break;

        case Qt::Key_Comma:
            _viewFrustumOffset.up -= VIEW_FRUSTUM_OFFSET_UP_DELTA;
            break;

        case Qt::Key_Period:
            _viewFrustumOffset.up += VIEW_FRUSTUM_OFFSET_UP_DELTA;
            break;

        default:
            break;
    }
}

void Menu::addDisabledActionAndSeparator(QMenu* destinationMenu, const QString& actionName, int menuItemLocation) {
    QAction* actionBefore = NULL;
    if (menuItemLocation >= 0 && destinationMenu->actions().size() > menuItemLocation) {
        actionBefore = destinationMenu->actions()[menuItemLocation];
    }
    if (actionBefore) {
        QAction* separator = new QAction("",destinationMenu);
        destinationMenu->insertAction(actionBefore, separator);
        separator->setSeparator(true);

        QAction* separatorText = new QAction(actionName,destinationMenu);
        separatorText->setEnabled(false);
        destinationMenu->insertAction(actionBefore, separatorText);

    } else {
        destinationMenu->addSeparator();
        (destinationMenu->addAction(actionName))->setEnabled(false);
    }
}

QAction* Menu::addActionToQMenuAndActionHash(QMenu* destinationMenu,
                                             const QString& actionName,
                                             const QKeySequence& shortcut,
                                             const QObject* receiver,
                                             const char* member,
                                             QAction::MenuRole role,
                                             int menuItemLocation) {
    QAction* action = NULL;
    QAction* actionBefore = NULL;

    if (menuItemLocation >= 0 && destinationMenu->actions().size() > menuItemLocation) {
        actionBefore = destinationMenu->actions()[menuItemLocation];
    }

    if (!actionBefore) {
        if (receiver && member) {
            action = destinationMenu->addAction(actionName, receiver, member, shortcut);
        } else {
            action = destinationMenu->addAction(actionName);
            action->setShortcut(shortcut);
        }
    } else {
        action = new QAction(actionName, destinationMenu);
        action->setShortcut(shortcut);
        destinationMenu->insertAction(actionBefore, action);

        if (receiver && member) {
            connect(action, SIGNAL(triggered()), receiver, member);
        }
    }
    action->setMenuRole(role);

    _actionHash.insert(actionName, action);

    return action;
}

QAction* Menu::addActionToQMenuAndActionHash(QMenu* destinationMenu,
                                             QAction* action,
                                             const QString& actionName,
                                             const QKeySequence& shortcut,
                                             QAction::MenuRole role,
                                             int menuItemLocation) {
    QAction* actionBefore = NULL;

    if (menuItemLocation >= 0 && destinationMenu->actions().size() > menuItemLocation) {
        actionBefore = destinationMenu->actions()[menuItemLocation];
    }

    if (!actionName.isEmpty()) {
        action->setText(actionName);
    }

    if (shortcut != 0) {
        action->setShortcut(shortcut);
    }

    if (role != QAction::NoRole) {
        action->setMenuRole(role);
    }

    if (!actionBefore) {
        destinationMenu->addAction(action);
    } else {
        destinationMenu->insertAction(actionBefore, action);
    }

    _actionHash.insert(action->text(), action);

    return action;
}

QAction* Menu::addCheckableActionToQMenuAndActionHash(QMenu* destinationMenu,
                                                      const QString& actionName,
                                                      const QKeySequence& shortcut,
                                                      const bool checked,
                                                      const QObject* receiver,
                                                      const char* member,
                                                      int menuItemLocation) {

    QAction* action = addActionToQMenuAndActionHash(destinationMenu, actionName, shortcut, receiver, member,
                                                        QAction::NoRole, menuItemLocation);
    action->setCheckable(true);
    action->setChecked(checked);
    connect(action, SIGNAL(changed()), Application::getInstance(), SLOT(bumpSettings()));

    return action;
}

void Menu::removeAction(QMenu* menu, const QString& actionName) {
    menu->removeAction(_actionHash.value(actionName));
}

void Menu::setIsOptionChecked(const QString& menuOption, bool isChecked) {
    QAction* menu = _actionHash.value(menuOption);
    if (menu) {
        menu->setChecked(isChecked);
    }
}

bool Menu::isOptionChecked(const QString& menuOption) {
    QAction* menu = _actionHash.value(menuOption);
    if (menu) {
        return menu->isChecked();
    }
    return false;
}

void Menu::triggerOption(const QString& menuOption) {
    QAction* action = _actionHash.value(menuOption);
    if (action) {
        action->trigger();
    } else {
        qDebug() << "NULL Action for menuOption '" << menuOption << "'";
    }
}

QAction* Menu::getActionForOption(const QString& menuOption) {
    return _actionHash.value(menuOption);
}

void Menu::aboutApp() {
    InfoView::forcedShow();
}

void sendFakeEnterEvent() {
    QPoint lastCursorPosition = QCursor::pos();
    QGLWidget* glWidget = Application::getInstance()->getGLWidget();

    QPoint windowPosition = glWidget->mapFromGlobal(lastCursorPosition);
    QEnterEvent enterEvent = QEnterEvent(windowPosition, windowPosition, lastCursorPosition);
    QCoreApplication::sendEvent(glWidget, &enterEvent);
}

const float DIALOG_RATIO_OF_WINDOW = 0.30f;

void Menu::loginForCurrentDomain() {
    LoginDialog* loginDialog = new LoginDialog(Application::getInstance()->getWindow());
    loginDialog->show();
    loginDialog->resizeAndPosition(false);
}

void Menu::editPreferences() {
    if (!_preferencesDialog) {
        _preferencesDialog = new PreferencesDialog(Application::getInstance()->getWindow());
        _preferencesDialog->show();
    } else {
        _preferencesDialog->close();
    }
}

void Menu::editAttachments() {
    if (!_attachmentsDialog) {
        _attachmentsDialog = new AttachmentsDialog();
        _attachmentsDialog->show();
    } else {
        _attachmentsDialog->close();
    }
}

void Menu::editAnimations() {
    if (!_animationsDialog) {
        _animationsDialog = new AnimationsDialog();
        _animationsDialog->show();
    } else {
        _animationsDialog->close();
    }
}

void Menu::goToDomain(const QString newDomain) {
    if (NodeList::getInstance()->getDomainHandler().getHostname() != newDomain) {
        // send a node kill request, indicating to other clients that they should play the "disappeared" effect
        Application::getInstance()->getAvatar()->sendKillAvatar();

        // give our nodeList the new domain-server hostname
        NodeList::getInstance()->getDomainHandler().setHostname(newDomain);
    }
}

void Menu::goToDomainDialog() {

    QString currentDomainHostname = NodeList::getInstance()->getDomainHandler().getHostname();

    if (NodeList::getInstance()->getDomainHandler().getPort() != DEFAULT_DOMAIN_SERVER_PORT) {
        // add the port to the currentDomainHostname string if it is custom
        currentDomainHostname.append(QString(":%1").arg(NodeList::getInstance()->getDomainHandler().getPort()));
    }

    QInputDialog domainDialog(Application::getInstance()->getWindow());
    domainDialog.setWindowTitle("Go to Domain");
    domainDialog.setLabelText("Domain server:");
    domainDialog.setTextValue(currentDomainHostname);
    domainDialog.setWindowFlags(Qt::Sheet);
    domainDialog.resize(domainDialog.parentWidget()->size().width() * DIALOG_RATIO_OF_WINDOW, domainDialog.size().height());

    int dialogReturn = domainDialog.exec();
    if (dialogReturn == QDialog::Accepted) {
        QString newHostname(DEFAULT_DOMAIN_HOSTNAME);

        if (domainDialog.textValue().size() > 0) {
            // the user input a new hostname, use that
            newHostname = domainDialog.textValue();
        }

        goToDomain(newHostname);
    }

    sendFakeEnterEvent();
}

void Menu::goToOrientation(QString orientation) {
    LocationManager::getInstance().goToOrientation(orientation);
}

bool Menu::goToDestination(QString destination) {
    return LocationManager::getInstance().goToDestination(destination);
}

void Menu::goTo(QString destination) {
    LocationManager::getInstance().goTo(destination);
}

void Menu::goTo() {

    QInputDialog gotoDialog(Application::getInstance()->getWindow());
    gotoDialog.setWindowTitle("Go to");
    gotoDialog.setLabelText("Destination or URL:\n @user, #place, hifi://domain/location/orientation");
    QString destination = QString();

    gotoDialog.setTextValue(destination);
    gotoDialog.setWindowFlags(Qt::Sheet);
    gotoDialog.resize(gotoDialog.parentWidget()->size().width() * DIALOG_RATIO_OF_WINDOW, gotoDialog.size().height());

    int dialogReturn = gotoDialog.exec();
    if (dialogReturn == QDialog::Accepted && !gotoDialog.textValue().isEmpty()) {
        QString desiredDestination = gotoDialog.textValue();
        if (!goToURL(desiredDestination)) {;
            goTo(desiredDestination);
        }
    }
    sendFakeEnterEvent();
}

bool Menu::goToURL(QString location) {
    if (location.startsWith(CUSTOM_URL_SCHEME + "//")) {
        QStringList urlParts = location.remove(0, CUSTOM_URL_SCHEME.length() + 2).split('/', QString::SkipEmptyParts);

        if (urlParts.count() > 1) {
            // if url has 2 or more parts, the first one is domain name
            QString domain = urlParts[0];

            // second part is either a destination coordinate or
            // a place name
            QString destination = urlParts[1];

            // any third part is an avatar orientation.
            QString orientation = urlParts.count() > 2 ? urlParts[2] : QString();

            goToDomain(domain);

            // goto either @user, #place, or x-xx,y-yy,z-zz
            // style co-ordinate.
            goTo(destination);

            if (!orientation.isEmpty()) {
                // location orientation
                goToOrientation(orientation);
            }
        } else if (urlParts.count() == 1) {
            QString destination = urlParts[0];

            // If this starts with # or @, treat it as a user/location, otherwise treat it as a domain
            if (destination[0] == '#' || destination[0] == '@') {
                goTo(destination);
            } else {
                goToDomain(destination);
            }
        }
        return true;
    }
    return false;
}

void Menu::goToUser(const QString& user) {
    LocationManager* manager = &LocationManager::getInstance();
    manager->goTo(user);
    connect(manager, &LocationManager::multipleDestinationsFound, this, &Menu::multipleDestinationsDecision);
}

/// Open a url, shortcutting any "hifi" scheme URLs to the local application.
void Menu::openUrl(const QUrl& url) {
    if (url.scheme() == "hifi") {
        goToURL(url.toString());
    } else {
        QDesktopServices::openUrl(url);
    }
}

void Menu::multipleDestinationsDecision(const QJsonObject& userData, const QJsonObject& placeData) {
    QMessageBox msgBox;
    msgBox.setText("Both user and location exists with same name");
    msgBox.setInformativeText("Where you wanna go?");
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Open);
    msgBox.button(QMessageBox::Ok)->setText("User");
    msgBox.button(QMessageBox::Open)->setText("Place");
    int userResponse = msgBox.exec();

    if (userResponse == QMessageBox::Ok) {
        Application::getInstance()->getAvatar()->goToLocationFromResponse(userData);
    } else if (userResponse == QMessageBox::Open) {
        Application::getInstance()->getAvatar()->goToLocationFromResponse(userData);
    }

    LocationManager* manager = reinterpret_cast<LocationManager*>(sender());
    disconnect(manager, &LocationManager::multipleDestinationsFound, this, &Menu::multipleDestinationsDecision);
}

void Menu::muteEnvironment() {
    int headerSize = numBytesForPacketHeaderGivenPacketType(PacketTypeMuteEnvironment);
    int packetSize = headerSize + sizeof(glm::vec3) + sizeof(float);

    glm::vec3 position = Application::getInstance()->getAvatar()->getPosition();

    char* packet = (char*)malloc(packetSize);
    populatePacketHeader(packet, PacketTypeMuteEnvironment);
    memcpy(packet + headerSize, &position, sizeof(glm::vec3));
    memcpy(packet + headerSize + sizeof(glm::vec3), &MUTE_RADIUS, sizeof(float));

    QByteArray mutePacket(packet, packetSize);

    // grab our audio mixer from the NodeList, if it exists
    SharedNodePointer audioMixer = NodeList::getInstance()->soloNodeOfType(NodeType::AudioMixer);

    if (audioMixer) {
        // send off this mute packet
        NodeList::getInstance()->writeDatagram(mutePacket, audioMixer);
    }

    free(packet);
}

void Menu::goToLocation() {
    MyAvatar* myAvatar = Application::getInstance()->getAvatar();
    glm::vec3 avatarPos = myAvatar->getPosition();
    QString currentLocation = QString("%1, %2, %3").arg(QString::number(avatarPos.x),
                                                        QString::number(avatarPos.y), QString::number(avatarPos.z));

    QInputDialog coordinateDialog(Application::getInstance()->getWindow());
    coordinateDialog.setWindowTitle("Go to Location");
    coordinateDialog.setLabelText("Coordinate as x,y,z:");
    coordinateDialog.setTextValue(currentLocation);
    coordinateDialog.setWindowFlags(Qt::Sheet);
    coordinateDialog.resize(coordinateDialog.parentWidget()->size().width() * 0.30, coordinateDialog.size().height());

    int dialogReturn = coordinateDialog.exec();
    if (dialogReturn == QDialog::Accepted && !coordinateDialog.textValue().isEmpty()) {
        goToDestination(coordinateDialog.textValue());
    }

    sendFakeEnterEvent();
}

void Menu::namedLocationCreated(LocationManager::NamedLocationCreateResponse response) {

    if (response == LocationManager::Created) {
        return;
    }

    QMessageBox msgBox;
    switch (response) {
        case LocationManager::AlreadyExists:
            msgBox.setText("That name has been already claimed, try something else.");
            break;
        default:
            msgBox.setText("An unexpected error has occurred, please try again later.");
            break;
    }

    msgBox.exec();
}

void Menu::nameLocation() {
    // check if user is logged in or show login dialog if not

    AccountManager& accountManager = AccountManager::getInstance();
    if (!accountManager.isLoggedIn()) {
        QMessageBox msgBox;
        msgBox.setText("We need to tie this location to your username.");
        msgBox.setInformativeText("Please login first, then try naming the location again.");
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.button(QMessageBox::Ok)->setText("Login");
        if (msgBox.exec() == QMessageBox::Ok) {
            loginForCurrentDomain();
        }

        return;
    }

    QInputDialog nameDialog(Application::getInstance()->getWindow());
    nameDialog.setWindowTitle("Name this location");
    nameDialog.setLabelText("Name this location, then share that name with others.\n"
                            "When they come here, they'll be in the same location and orientation\n"
                            "(wherever you are standing and looking now) as you.\n\n"
                            "Location name:");

    nameDialog.setWindowFlags(Qt::Sheet);
    nameDialog.resize((int) (nameDialog.parentWidget()->size().width() * 0.30), nameDialog.size().height());

    if (nameDialog.exec() == QDialog::Accepted) {

        QString locationName = nameDialog.textValue().trimmed();
        if (locationName.isEmpty()) {
            return;
        }

        MyAvatar* myAvatar = Application::getInstance()->getAvatar();
        LocationManager* manager = new LocationManager();
        connect(manager, &LocationManager::creationCompleted, this, &Menu::namedLocationCreated);
        NamedLocation* location = new NamedLocation(locationName,
                                                    myAvatar->getPosition(), myAvatar->getOrientation(),
                                                    NodeList::getInstance()->getDomainHandler().getHostname());
        manager->createNamedLocation(location);
    }
}

void Menu::pasteToVoxel() {
    QInputDialog pasteToOctalCodeDialog(Application::getInstance()->getWindow());
    pasteToOctalCodeDialog.setWindowTitle("Paste to Voxel");
    pasteToOctalCodeDialog.setLabelText("Octal Code:");
    QString octalCode = "";
    pasteToOctalCodeDialog.setTextValue(octalCode);
    pasteToOctalCodeDialog.setWindowFlags(Qt::Sheet);
    pasteToOctalCodeDialog.resize(pasteToOctalCodeDialog.parentWidget()->size().width() * DIALOG_RATIO_OF_WINDOW,
        pasteToOctalCodeDialog.size().height());

    int dialogReturn = pasteToOctalCodeDialog.exec();
    if (dialogReturn == QDialog::Accepted && !pasteToOctalCodeDialog.textValue().isEmpty()) {
        // we got an octalCode to paste to...
        QString locationToPaste = pasteToOctalCodeDialog.textValue();
        unsigned char* octalCodeDestination = hexStringToOctalCode(locationToPaste);

        // check to see if it was a legit octcode...
        if (locationToPaste == octalCodeToHexString(octalCodeDestination)) {
            Application::getInstance()->pasteVoxelsToOctalCode(octalCodeDestination);
        } else {
            qDebug() << "Problem with octcode...";
        }
    }

    sendFakeEnterEvent();
}

void Menu::toggleLoginMenuItem() {
    AccountManager& accountManager = AccountManager::getInstance();

    disconnect(_loginAction, 0, 0, 0);

    if (accountManager.isLoggedIn()) {
        // change the menu item to logout
        _loginAction->setText("Logout " + accountManager.getAccountInfo().getUsername());
        connect(_loginAction, &QAction::triggered, &accountManager, &AccountManager::logout);
    } else {
        // change the menu item to login
        _loginAction->setText("Login");

        connect(_loginAction, &QAction::triggered, this, &Menu::loginForCurrentDomain);
    }
}

void Menu::bandwidthDetails() {
    if (! _bandwidthDialog) {
        _bandwidthDialog = new BandwidthDialog(Application::getInstance()->getGLWidget(),
                                               Application::getInstance()->getBandwidthMeter());
        connect(_bandwidthDialog, SIGNAL(closed()), SLOT(bandwidthDetailsClosed()));

        _bandwidthDialog->show();
    }
    _bandwidthDialog->raise();
}

void Menu::showMetavoxelEditor() {
    if (!_MetavoxelEditor) {
        _MetavoxelEditor = new MetavoxelEditor();
    }
    _MetavoxelEditor->raise();
}

void Menu::showScriptEditor() {
    if(!_ScriptEditor || !_ScriptEditor->isVisible()) {
        _ScriptEditor = new ScriptEditorWindow();
    }
    _ScriptEditor->raise();
}

void Menu::showChat() {
    if (AccountManager::getInstance().isLoggedIn()) {
        QMainWindow* mainWindow = Application::getInstance()->getWindow();
        if (!_chatWindow) {
            _chatWindow = new ChatWindow(mainWindow);
        }

        if (_chatWindow->isHidden()) {
            _chatWindow->show();
        }
    } else {
        Application::getInstance()->getTrayIcon()->showMessage("Interface", "You need to login to be able to chat with others on this domain.");
    }
}

void Menu::toggleChat() {
#ifdef HAVE_QXMPP
    _chatAction->setEnabled(XmppClient::getInstance().getXMPPClient().isConnected());
    if (!_chatAction->isEnabled() && _chatWindow && AccountManager::getInstance().isLoggedIn()) {
        if (_chatWindow->isHidden()) {
            _chatWindow->show();
        } else {
            _chatWindow->hide();
        }
    }
#endif
}

void Menu::audioMuteToggled() {
    QAction *muteAction = _actionHash.value(MenuOption::MuteAudio);
    muteAction->setChecked(Application::getInstance()->getAudio()->getMuted());
}

void Menu::bandwidthDetailsClosed() {
    if (_bandwidthDialog) {
        delete _bandwidthDialog;
        _bandwidthDialog = NULL;
    }
}

void Menu::octreeStatsDetails() {
    if (!_octreeStatsDialog) {
        _octreeStatsDialog = new OctreeStatsDialog(Application::getInstance()->getGLWidget(),
                                                 Application::getInstance()->getOcteeSceneStats());
        connect(_octreeStatsDialog, SIGNAL(closed()), SLOT(octreeStatsDetailsClosed()));
        _octreeStatsDialog->show();
    }
    _octreeStatsDialog->raise();
}

void Menu::octreeStatsDetailsClosed() {
    if (_octreeStatsDialog) {
        delete _octreeStatsDialog;
        _octreeStatsDialog = NULL;
    }
}

QString Menu::getLODFeedbackText() {
    // determine granularity feedback
    int boundaryLevelAdjust = getBoundaryLevelAdjust();
    QString granularityFeedback;

    switch (boundaryLevelAdjust) {
        case 0: {
            granularityFeedback = QString("at standard granularity.");
        } break;
        case 1: {
            granularityFeedback = QString("at half of standard granularity.");
        } break;
        case 2: {
            granularityFeedback = QString("at a third of standard granularity.");
        } break;
        default: {
            granularityFeedback = QString("at 1/%1th of standard granularity.").arg(boundaryLevelAdjust + 1);
        } break;
    }

    // distance feedback
    float voxelSizeScale = getVoxelSizeScale();
    float relativeToDefault = voxelSizeScale / DEFAULT_OCTREE_SIZE_SCALE;
    QString result;
    if (relativeToDefault > 1.01) {
        result = QString("%1 further %2").arg(relativeToDefault,8,'f',2).arg(granularityFeedback);
    } else if (relativeToDefault > 0.99) {
            result = QString("the default distance %1").arg(granularityFeedback);
    } else {
        result = QString("%1 of default %2").arg(relativeToDefault,8,'f',3).arg(granularityFeedback);
    }
    return result;
}

void Menu::autoAdjustLOD(float currentFPS) {
    // NOTE: our first ~100 samples at app startup are completely all over the place, and we don't
    // really want to count them in our average, so we will ignore the real frame rates and stuff
    // our moving average with simulated good data
    const int IGNORE_THESE_SAMPLES = 100;
    const float ASSUMED_FPS = 60.0f;
    if (_fpsAverage.getSampleCount() < IGNORE_THESE_SAMPLES) {
        currentFPS = ASSUMED_FPS;
    }
    _fpsAverage.updateAverage(currentFPS);
    _fastFPSAverage.updateAverage(currentFPS);

    quint64 now = usecTimestampNow();

    const quint64 ADJUST_AVATAR_LOD_DOWN_DELAY = 1000 * 1000;
    if (_automaticAvatarLOD) {
        if (_fastFPSAverage.getAverage() < _avatarLODDecreaseFPS) {
            if (now - _lastAvatarDetailDrop > ADJUST_AVATAR_LOD_DOWN_DELAY) {
                // attempt to lower the detail in proportion to the fps difference
                float targetFps = (_avatarLODDecreaseFPS + _avatarLODIncreaseFPS) * 0.5f;
                float averageFps = _fastFPSAverage.getAverage();
                const float MAXIMUM_MULTIPLIER_SCALE = 2.0f;
                _avatarLODDistanceMultiplier = qMin(MAXIMUM_AVATAR_LOD_DISTANCE_MULTIPLIER, _avatarLODDistanceMultiplier *
                    (averageFps < EPSILON ? MAXIMUM_MULTIPLIER_SCALE :
                        qMin(MAXIMUM_MULTIPLIER_SCALE, targetFps / averageFps)));
                _lastAvatarDetailDrop = now;
            }
        } else if (_fastFPSAverage.getAverage() > _avatarLODIncreaseFPS) {
            // let the detail level creep slowly upwards
            const float DISTANCE_DECREASE_RATE = 0.05f;
            _avatarLODDistanceMultiplier = qMax(MINIMUM_AVATAR_LOD_DISTANCE_MULTIPLIER,
                _avatarLODDistanceMultiplier - DISTANCE_DECREASE_RATE);
        }
    }

    bool changed = false;
    quint64 elapsed = now - _lastAdjust;

    if (elapsed > ADJUST_LOD_DOWN_DELAY && _fpsAverage.getAverage() < ADJUST_LOD_DOWN_FPS
            && _voxelSizeScale > ADJUST_LOD_MIN_SIZE_SCALE) {

        _voxelSizeScale *= ADJUST_LOD_DOWN_BY;
        if (_voxelSizeScale < ADJUST_LOD_MIN_SIZE_SCALE) {
            _voxelSizeScale = ADJUST_LOD_MIN_SIZE_SCALE;
        }
        changed = true;
        _lastAdjust = now;
        qDebug() << "adjusting LOD down... average fps for last approximately 5 seconds=" << _fpsAverage.getAverage()
                     << "_voxelSizeScale=" << _voxelSizeScale;
    }

    if (elapsed > ADJUST_LOD_UP_DELAY && _fpsAverage.getAverage() > ADJUST_LOD_UP_FPS
            && _voxelSizeScale < ADJUST_LOD_MAX_SIZE_SCALE) {
        _voxelSizeScale *= ADJUST_LOD_UP_BY;
        if (_voxelSizeScale > ADJUST_LOD_MAX_SIZE_SCALE) {
            _voxelSizeScale = ADJUST_LOD_MAX_SIZE_SCALE;
        }
        changed = true;
        _lastAdjust = now;
        qDebug() << "adjusting LOD up... average fps for last approximately 5 seconds=" << _fpsAverage.getAverage()
                     << "_voxelSizeScale=" << _voxelSizeScale;
    }

    if (changed) {
        if (_lodToolsDialog) {
            _lodToolsDialog->reloadSliders();
        }
    }
}

void Menu::resetLODAdjust() {
    _fpsAverage.reset();
    _fastFPSAverage.reset();
    _lastAvatarDetailDrop = _lastAdjust = usecTimestampNow();
}

void Menu::setVoxelSizeScale(float sizeScale) {
    _voxelSizeScale = sizeScale;
}

void Menu::setBoundaryLevelAdjust(int boundaryLevelAdjust) {
    _boundaryLevelAdjust = boundaryLevelAdjust;
}

void Menu::lodTools() {
    if (!_lodToolsDialog) {
        _lodToolsDialog = new LodToolsDialog(Application::getInstance()->getGLWidget());
        connect(_lodToolsDialog, SIGNAL(closed()), SLOT(lodToolsClosed()));
        _lodToolsDialog->show();
    }
    _lodToolsDialog->raise();
}

void Menu::lodToolsClosed() {
    if (_lodToolsDialog) {
        delete _lodToolsDialog;
        _lodToolsDialog = NULL;
    }
}

void Menu::cycleFrustumRenderMode() {
    _frustumDrawMode = (FrustumDrawMode)((_frustumDrawMode + 1) % FRUSTUM_DRAW_MODE_COUNT);
    updateFrustumRenderModeAction();
}

void Menu::runTests() {
    runTimingTests();
}

void Menu::updateFrustumRenderModeAction() {
    QAction* frustumRenderModeAction = _actionHash.value(MenuOption::FrustumRenderMode);
    switch (_frustumDrawMode) {
        default:
        case FRUSTUM_DRAW_MODE_ALL:
            frustumRenderModeAction->setText("Render Mode - All");
            break;
        case FRUSTUM_DRAW_MODE_VECTORS:
            frustumRenderModeAction->setText("Render Mode - Vectors");
            break;
        case FRUSTUM_DRAW_MODE_PLANES:
            frustumRenderModeAction->setText("Render Mode - Planes");
            break;
        case FRUSTUM_DRAW_MODE_NEAR_PLANE:
            frustumRenderModeAction->setText("Render Mode - Near");
            break;
        case FRUSTUM_DRAW_MODE_FAR_PLANE:
            frustumRenderModeAction->setText("Render Mode - Far");
            break;
        case FRUSTUM_DRAW_MODE_KEYHOLE:
            frustumRenderModeAction->setText("Render Mode - Keyhole");
            break;
    }
}

void Menu::addAvatarCollisionSubMenu(QMenu* overMenu) {
    // add avatar collisions subMenu to overMenu
    QMenu* subMenu = overMenu->addMenu("Collision Options");

    Application* appInstance = Application::getInstance();
    QObject* avatar = appInstance->getAvatar();
    addCheckableActionToQMenuAndActionHash(subMenu, MenuOption::CollideWithEnvironment,
            0, false, avatar, SLOT(updateCollisionGroups()));
    addCheckableActionToQMenuAndActionHash(subMenu, MenuOption::CollideWithAvatars,
            0, true, avatar, SLOT(updateCollisionGroups()));
    addCheckableActionToQMenuAndActionHash(subMenu, MenuOption::CollideWithVoxels,
            0, false, avatar, SLOT(updateCollisionGroups()));
    addCheckableActionToQMenuAndActionHash(subMenu, MenuOption::CollideWithParticles,
            0, true, avatar, SLOT(updateCollisionGroups()));
}

QAction* Menu::getActionFromName(const QString& menuName, QMenu* menu) {
    QList<QAction*> menuActions;
    if (menu) {
        menuActions = menu->actions();
    } else {
        menuActions = actions();
    }

    foreach (QAction* menuAction, menuActions) {
        if (menuName == menuAction->text()) {
            return menuAction;
        }
    }
    return NULL;
}

QMenu* Menu::getSubMenuFromName(const QString& menuName, QMenu* menu) {
    QAction* action = getActionFromName(menuName, menu);
    if (action) {
        return action->menu();
    }
    return NULL;
}

QMenu* Menu::getMenuParent(const QString& menuName, QString& finalMenuPart) {
    QStringList menuTree = menuName.split(">");
    QMenu* parent = NULL;
    QMenu* menu = NULL;
    foreach (QString menuTreePart, menuTree) {
        parent = menu;
        finalMenuPart = menuTreePart.trimmed();
        menu = getSubMenuFromName(finalMenuPart, parent);
        if (!menu) {
            break;
        }
    }
    return parent;
}

QMenu* Menu::getMenu(const QString& menuName) {
    QStringList menuTree = menuName.split(">");
    QMenu* parent = NULL;
    QMenu* menu = NULL;
    int item = 0;
    foreach (QString menuTreePart, menuTree) {
        menu = getSubMenuFromName(menuTreePart.trimmed(), parent);
        if (!menu) {
            break;
        }
        parent = menu;
        item++;
    }
    return menu;
}

QAction* Menu::getMenuAction(const QString& menuName) {
    QStringList menuTree = menuName.split(">");
    QMenu* parent = NULL;
    QAction* action = NULL;
    foreach (QString menuTreePart, menuTree) {
        action = getActionFromName(menuTreePart.trimmed(), parent);
        if (!action) {
            break;
        }
        parent = action->menu();
    }
    return action;
}

int Menu::findPositionOfMenuItem(QMenu* menu, const QString& searchMenuItem) {
    int position = 0;
    foreach(QAction* action, menu->actions()) {
        if (action->text() == searchMenuItem) {
            return position;
        }
        position++;
    }
    return UNSPECIFIED_POSITION; // not found
}

int Menu::positionBeforeSeparatorIfNeeded(QMenu* menu, int requestedPosition) {
    QList<QAction*> menuActions = menu->actions();
    if (requestedPosition > 1 && requestedPosition < menuActions.size()) {
        QAction* beforeRequested = menuActions[requestedPosition - 1];
        if (beforeRequested->isSeparator()) {
            requestedPosition--;
        }
    }
    return requestedPosition;
}


QMenu* Menu::addMenu(const QString& menuName) {
    QStringList menuTree = menuName.split(">");
    QMenu* addTo = NULL;
    QMenu* menu = NULL;
    foreach (QString menuTreePart, menuTree) {
        menu = getSubMenuFromName(menuTreePart.trimmed(), addTo);
        if (!menu) {
            if (!addTo) {
                menu = QMenuBar::addMenu(menuTreePart.trimmed());
            } else {
                menu = addTo->addMenu(menuTreePart.trimmed());
            }
        }
        addTo = menu;
    }

    QMenuBar::repaint();
    return menu;
}

void Menu::removeMenu(const QString& menuName) {
    QAction* action = getMenuAction(menuName);

    // only proceed if the menu actually exists
    if (action) {
        QString finalMenuPart;
        QMenu* parent = getMenuParent(menuName, finalMenuPart);
        if (parent) {
            parent->removeAction(action);
        } else {
            QMenuBar::removeAction(action);
        }

        QMenuBar::repaint();
    }
}

void Menu::addSeparator(const QString& menuName, const QString& separatorName) {
    QMenu* menuObj = getMenu(menuName);
    if (menuObj) {
        addDisabledActionAndSeparator(menuObj, separatorName);
    }
}

void Menu::removeSeparator(const QString& menuName, const QString& separatorName) {
    QMenu* menu = getMenu(menuName);
    bool separatorRemoved = false;
    if (menu) {
        int textAt = findPositionOfMenuItem(menu, separatorName);
        QList<QAction*> menuActions = menu->actions();
        QAction* separatorText = menuActions[textAt];
        if (textAt > 0 && textAt < menuActions.size()) {
            QAction* separatorLine = menuActions[textAt - 1];
            if (separatorLine) {
                if (separatorLine->isSeparator()) {
                    menu->removeAction(separatorText);
                    menu->removeAction(separatorLine);
                    separatorRemoved = true;
                }
            }
        }
    }
    if (separatorRemoved) {
        QMenuBar::repaint();
    }
}

void Menu::addMenuItem(const MenuItemProperties& properties) {
    QMenu* menuObj = getMenu(properties.menuName);
    if (menuObj) {
        QShortcut* shortcut = NULL;
        if (!properties.shortcutKeySequence.isEmpty()) {
            shortcut = new QShortcut(properties.shortcutKeySequence, this);
        }

        // check for positioning requests
        int requestedPosition = properties.position;
        if (requestedPosition == UNSPECIFIED_POSITION && !properties.beforeItem.isEmpty()) {
            requestedPosition = findPositionOfMenuItem(menuObj, properties.beforeItem);
            // double check that the requested location wasn't a separator label
            requestedPosition = positionBeforeSeparatorIfNeeded(menuObj, requestedPosition);
        }
        if (requestedPosition == UNSPECIFIED_POSITION && !properties.afterItem.isEmpty()) {
            int afterPosition = findPositionOfMenuItem(menuObj, properties.afterItem);
            if (afterPosition != UNSPECIFIED_POSITION) {
                requestedPosition = afterPosition + 1;
            }
        }

        QAction* menuItemAction = NULL;
        if (properties.isSeparator) {
            addDisabledActionAndSeparator(menuObj, properties.menuItemName, requestedPosition);
        } else if (properties.isCheckable) {
            menuItemAction = addCheckableActionToQMenuAndActionHash(menuObj, properties.menuItemName,
                                    properties.shortcutKeySequence, properties.isChecked,
                                    MenuScriptingInterface::getInstance(), SLOT(menuItemTriggered()), requestedPosition);
        } else {
            menuItemAction = addActionToQMenuAndActionHash(menuObj, properties.menuItemName, properties.shortcutKeySequence,
                                    MenuScriptingInterface::getInstance(), SLOT(menuItemTriggered()),
                                    QAction::NoRole, requestedPosition);
        }
        if (shortcut && menuItemAction) {
            connect(shortcut, SIGNAL(activated()), menuItemAction, SLOT(trigger()));
        }
        QMenuBar::repaint();
    }
}

void Menu::removeMenuItem(const QString& menu, const QString& menuitem) {
    QMenu* menuObj = getMenu(menu);
    if (menuObj) {
        removeAction(menuObj, menuitem);
    }
    QMenuBar::repaint();
};

QString Menu::getSnapshotsLocation() const {
    if (_snapshotsLocation.isNull() || _snapshotsLocation.isEmpty() || QDir(_snapshotsLocation).exists() == false) {
        return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    return _snapshotsLocation;
}

