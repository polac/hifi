//
//  PreferencesDialog.cpp
//  interface/src/ui
//
//  Created by Stojce Slavkovski on 2/20/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//


#include "Application.h"
#include "Menu.h"
#include "PreferencesDialog.h"
#include "ModelsBrowser.h"

const int SCROLL_PANEL_BOTTOM_MARGIN = 30;
const int OK_BUTTON_RIGHT_MARGIN = 30;
const int BUTTONS_TOP_MARGIN = 24;

PreferencesDialog::PreferencesDialog(QWidget* parent, Qt::WindowFlags flags) : FramelessDialog(parent, flags, POSITION_LEFT) {

    ui.setupUi(this);
    setStyleSheetFile("styles/preferences.qss");
    loadPreferences();
    connect(ui.closeButton, &QPushButton::clicked, this, &QDialog::close);

    connect(ui.buttonBrowseHead, &QPushButton::clicked, this, &PreferencesDialog::openHeadModelBrowser);
    connect(ui.buttonBrowseBody, &QPushButton::clicked, this, &PreferencesDialog::openBodyModelBrowser);
    connect(ui.buttonBrowseLocation, &QPushButton::clicked, this, &PreferencesDialog::openSnapshotLocationBrowser);
}

void PreferencesDialog::accept() {
    savePreferences();
    close();
}

void PreferencesDialog::setHeadUrl(QString modelUrl) {
    ui.faceURLEdit->setText(modelUrl);
}

void PreferencesDialog::setSkeletonUrl(QString modelUrl) {
    ui.skeletonURLEdit->setText(modelUrl);
}

void PreferencesDialog::openHeadModelBrowser() {
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
    show();

    ModelsBrowser modelBrowser(HEAD_MODEL);
    connect(&modelBrowser, &ModelsBrowser::selected, this, &PreferencesDialog::setHeadUrl);
    modelBrowser.browse();

    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    show();
}

void PreferencesDialog::openBodyModelBrowser() {
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
    show();

    ModelsBrowser modelBrowser(SKELETON_MODEL);
    connect(&modelBrowser, &ModelsBrowser::selected, this, &PreferencesDialog::setSkeletonUrl);
    modelBrowser.browse();

    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    show();
}

void PreferencesDialog::openSnapshotLocationBrowser() {
    setWindowFlags(windowFlags() & ~Qt::WindowStaysOnTopHint);
    QString dir = QFileDialog::getExistingDirectory(this, tr("Snapshots Location"),
                                                    QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                                                    QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (!dir.isNull() && !dir.isEmpty()) {
        ui.snapshotLocationEdit->setText(dir);
    }
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
}

void PreferencesDialog::resizeEvent(QResizeEvent *resizeEvent) {

    // keep buttons panel at the bottom
    ui.buttonsPanel->setGeometry(0, size().height() - ui.buttonsPanel->height(), size().width(), ui.buttonsPanel->height());

    // set width and height of srcollarea to match bottom panel and width
    ui.scrollArea->setGeometry(ui.scrollArea->geometry().x(), ui.scrollArea->geometry().y(),
                               size().width(),
                               size().height() - ui.buttonsPanel->height() -
                               SCROLL_PANEL_BOTTOM_MARGIN - ui.scrollArea->geometry().y());

    // move Save button to left position
    ui.defaultButton->move(size().width() - OK_BUTTON_RIGHT_MARGIN - ui.defaultButton->size().width(), BUTTONS_TOP_MARGIN);

    // move Save button to left position
    ui.cancelButton->move(ui.defaultButton->pos().x() - ui.cancelButton->size().width(), BUTTONS_TOP_MARGIN);

    // move close button
    ui.closeButton->move(size().width() - OK_BUTTON_RIGHT_MARGIN - ui.closeButton->size().width(), ui.closeButton->pos().y());
}

void PreferencesDialog::loadPreferences() {
    
    MyAvatar* myAvatar = Application::getInstance()->getAvatar();
    Menu* menuInstance = Menu::getInstance();

    _displayNameString = myAvatar->getDisplayName();
    ui.displayNameEdit->setText(_displayNameString);

    _faceURLString = myAvatar->getHead()->getFaceModel().getURL().toString();
    ui.faceURLEdit->setText(_faceURLString);

    _skeletonURLString = myAvatar->getSkeletonModel().getURL().toString();
    ui.skeletonURLEdit->setText(_skeletonURLString);

    ui.snapshotLocationEdit->setText(menuInstance->getSnapshotsLocation());

    ui.pupilDilationSlider->setValue(myAvatar->getHead()->getPupilDilation() *
                                     ui.pupilDilationSlider->maximum());
    
    ui.faceshiftEyeDeflectionSider->setValue(menuInstance->getFaceshiftEyeDeflection() *
                                             ui.faceshiftEyeDeflectionSider->maximum());
    
    ui.audioJitterSpin->setValue(menuInstance->getAudioJitterBufferSamples());

    ui.realWorldFieldOfViewSpin->setValue(menuInstance->getRealWorldFieldOfView());

    ui.fieldOfViewSpin->setValue(menuInstance->getFieldOfView());
    
    ui.leanScaleSpin->setValue(myAvatar->getLeanScale());
    
    ui.avatarScaleSpin->setValue(myAvatar->getScale());
    
    ui.maxVoxelsSpin->setValue(menuInstance->getMaxVoxels());
    
    ui.maxVoxelsPPSSpin->setValue(menuInstance->getMaxVoxelPacketsPerSecond());
}

void PreferencesDialog::savePreferences() {
    
    MyAvatar* myAvatar = Application::getInstance()->getAvatar();
    bool shouldDispatchIdentityPacket = false;
    
    QString displayNameStr(ui.displayNameEdit->text());
    if (displayNameStr != _displayNameString) {
        myAvatar->setDisplayName(displayNameStr);
        shouldDispatchIdentityPacket = true;
    }
    
    QUrl faceModelURL(ui.faceURLEdit->text());
    if (faceModelURL.toString() != _faceURLString) {
        // change the faceModelURL in the profile, it will also update this user's BlendFace
        myAvatar->setFaceModelURL(faceModelURL);
        shouldDispatchIdentityPacket = true;
    }

    QUrl skeletonModelURL(ui.skeletonURLEdit->text());
    if (skeletonModelURL.toString() != _skeletonURLString) {
        // change the skeletonModelURL in the profile, it will also update this user's Body
        myAvatar->setSkeletonModelURL(skeletonModelURL);
        shouldDispatchIdentityPacket = true;
    }
    
    if (shouldDispatchIdentityPacket) {
        myAvatar->sendIdentityPacket();
        Application::getInstance()->bumpSettings();
    }

    if (!ui.snapshotLocationEdit->text().isEmpty() && QDir(ui.snapshotLocationEdit->text()).exists()) {
        Menu::getInstance()->setSnapshotsLocation(ui.snapshotLocationEdit->text());
    }

    myAvatar->getHead()->setPupilDilation(ui.pupilDilationSlider->value() / (float)ui.pupilDilationSlider->maximum());
    myAvatar->setLeanScale(ui.leanScaleSpin->value());
    myAvatar->setClampedTargetScale(ui.avatarScaleSpin->value());
    
    Application::getInstance()->getVoxels()->setMaxVoxels(ui.maxVoxelsSpin->value());
    Application::getInstance()->resizeGL(Application::getInstance()->getGLWidget()->width(),
                                         Application::getInstance()->getGLWidget()->height());

    Menu::getInstance()->setRealWorldFieldOfView(ui.realWorldFieldOfViewSpin->value());
    
    Menu::getInstance()->setFieldOfView(ui.fieldOfViewSpin->value());

    Menu::getInstance()->setFaceshiftEyeDeflection(ui.faceshiftEyeDeflectionSider->value() /
                                                     (float)ui.faceshiftEyeDeflectionSider->maximum());
    Menu::getInstance()->setMaxVoxelPacketsPerSecond(ui.maxVoxelsPPSSpin->value());

    Menu::getInstance()->setAudioJitterBufferSamples(ui.audioJitterSpin->value());
    Application::getInstance()->getAudio()->setJitterBufferSamples(ui.audioJitterSpin->value());

    Application::getInstance()->resizeGL(Application::getInstance()->getGLWidget()->width(),
                                         Application::getInstance()->getGLWidget()->height());
}
