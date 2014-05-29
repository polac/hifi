//
//  AssignmentClient.cpp
//  assignment-client/src
//
//  Created by Stephen Birarda on 11/25/2013.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtCore/QProcess>
#include <QtCore/QThread>
#include <QtCore/QTimer>

#include <AccountManager.h>
#include <Assignment.h>
#include <Logging.h>
#include <NodeList.h>
#include <PacketHeaders.h>
#include <SharedUtil.h>


#include "AssignmentFactory.h"
#include "AssignmentThread.h"

#include "AssignmentClient.h"

const QString ASSIGNMENT_CLIENT_TARGET_NAME = "assignment-client";
const long long ASSIGNMENT_REQUEST_INTERVAL_MSECS = 1 * 1000;

SharedAssignmentPointer AssignmentClient::_currentAssignment;

int hifiSockAddrMeta = qRegisterMetaType<HifiSockAddr>("HifiSockAddr");

AssignmentClient::AssignmentClient(int &argc, char **argv) :
    QCoreApplication(argc, argv),
    _assignmentServerHostname(DEFAULT_ASSIGNMENT_SERVER_HOSTNAME)
{
    setOrganizationName("High Fidelity");
    setOrganizationDomain("highfidelity.io");
    setApplicationName("assignment-client");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    
    QStringList argumentList = arguments();
    
    // register meta type is required for queued invoke method on Assignment subclasses
    
    // set the logging target to the the CHILD_TARGET_NAME
    Logging::setTargetName(ASSIGNMENT_CLIENT_TARGET_NAME);
    
    const QString ASSIGNMENT_TYPE_OVVERIDE_OPTION = "-t";
    int argumentIndex = argumentList.indexOf(ASSIGNMENT_TYPE_OVVERIDE_OPTION);
    
    Assignment::Type requestAssignmentType = Assignment::AllTypes;
    
    if (argumentIndex != -1) {
        requestAssignmentType = (Assignment::Type) argumentList[argumentIndex + 1].toInt();
    }
    
    const QString ASSIGNMENT_POOL_OPTION = "--pool";
    
    argumentIndex = argumentList.indexOf(ASSIGNMENT_POOL_OPTION);
    
    QString assignmentPool;
    
    if (argumentIndex != -1) {
        assignmentPool = argumentList[argumentIndex + 1];
    }
    
    // setup our _requestAssignment member variable from the passed arguments
    _requestAssignment = Assignment(Assignment::RequestCommand, requestAssignmentType, assignmentPool);
    
    // check if we were passed a wallet UUID on the command line
    // this would represent where the user running AC wants funds sent to
    
    const QString ASSIGNMENT_WALLET_DESTINATION_ID_OPTION = "--wallet";
    if ((argumentIndex = argumentList.indexOf(ASSIGNMENT_WALLET_DESTINATION_ID_OPTION)) != -1) {
        QUuid walletUUID = QString(argumentList[argumentIndex + 1]);
        qDebug() << "The destination wallet UUID for credits is" << uuidStringWithoutCurlyBraces(walletUUID);
        _requestAssignment.setWalletUUID(walletUUID);
    }
    
    // create a NodeList as an unassigned client
    NodeList* nodeList = NodeList::createInstance(NodeType::Unassigned);
    
    // check for an overriden assignment server hostname
    const QString CUSTOM_ASSIGNMENT_SERVER_HOSTNAME_OPTION = "-a";
    
    argumentIndex = argumentList.indexOf(CUSTOM_ASSIGNMENT_SERVER_HOSTNAME_OPTION);
    
    if (argumentIndex != -1) {
        _assignmentServerHostname = argumentList[argumentIndex + 1];
        
        // set the custom assignment socket on our NodeList
        HifiSockAddr customAssignmentSocket = HifiSockAddr(_assignmentServerHostname, DEFAULT_DOMAIN_SERVER_PORT);
        
        nodeList->setAssignmentServerSocket(customAssignmentSocket);
    }
    
    // call a timer function every ASSIGNMENT_REQUEST_INTERVAL_MSECS to ask for assignment, if required
    qDebug() << "Waiting for assignment -" << _requestAssignment;
    
    QTimer* timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), SLOT(sendAssignmentRequest()));
    timer->start(ASSIGNMENT_REQUEST_INTERVAL_MSECS);
    
    // connect our readPendingDatagrams method to the readyRead() signal of the socket
    connect(&nodeList->getNodeSocket(), &QUdpSocket::readyRead, this, &AssignmentClient::readPendingDatagrams);
    
    // connections to AccountManager for authentication
    connect(&AccountManager::getInstance(), &AccountManager::authRequired,
            this, &AssignmentClient::handleAuthenticationRequest);
}

void AssignmentClient::sendAssignmentRequest() {
    if (!_currentAssignment) {
        NodeList::getInstance()->sendAssignment(_requestAssignment);
    }
}

void AssignmentClient::readPendingDatagrams() {
    NodeList* nodeList = NodeList::getInstance();
    
    QByteArray receivedPacket;
    HifiSockAddr senderSockAddr;
    
    while (nodeList->getNodeSocket().hasPendingDatagrams()) {
        receivedPacket.resize(nodeList->getNodeSocket().pendingDatagramSize());
        nodeList->getNodeSocket().readDatagram(receivedPacket.data(), receivedPacket.size(),
                                               senderSockAddr.getAddressPointer(), senderSockAddr.getPortPointer());
        
        if (nodeList->packetVersionAndHashMatch(receivedPacket)) {
            if (packetTypeForPacket(receivedPacket) == PacketTypeCreateAssignment) {
                // construct the deployed assignment from the packet data
                _currentAssignment = SharedAssignmentPointer(AssignmentFactory::unpackAssignment(receivedPacket));
                
                if (_currentAssignment) {
                    qDebug() << "Received an assignment -" << *_currentAssignment;
                    
                    // switch our DomainHandler hostname and port to whoever sent us the assignment
                    
                    nodeList->getDomainHandler().setSockAddr(senderSockAddr, _assignmentServerHostname);
                    nodeList->getDomainHandler().setAssignmentUUID(_currentAssignment->getUUID());
                    
                    qDebug() << "Destination IP for assignment is" << nodeList->getDomainHandler().getIP().toString();
                    
                    // start the deployed assignment
                    AssignmentThread* workerThread = new AssignmentThread(_currentAssignment, this);
                    
                    connect(workerThread, &QThread::started, _currentAssignment.data(), &ThreadedAssignment::run);
                    connect(_currentAssignment.data(), &ThreadedAssignment::finished, workerThread, &QThread::quit);
                    connect(_currentAssignment.data(), &ThreadedAssignment::finished,
                            this, &AssignmentClient::assignmentCompleted);
                    connect(workerThread, &QThread::finished, workerThread, &QThread::deleteLater);
                    
                    _currentAssignment->moveToThread(workerThread);
                    
                    // move the NodeList to the thread used for the _current assignment
                    nodeList->moveToThread(workerThread);
                    
                    // let the assignment handle the incoming datagrams for its duration
                    disconnect(&nodeList->getNodeSocket(), 0, this, 0);
                    connect(&nodeList->getNodeSocket(), &QUdpSocket::readyRead, _currentAssignment.data(),
                            &ThreadedAssignment::readPendingDatagrams);
                    
                    // Starts an event loop, and emits workerThread->started()
                    workerThread->start();
                } else {
                    qDebug() << "Received an assignment that could not be unpacked. Re-requesting.";
                }
            } else {
                // have the NodeList attempt to handle it
                nodeList->processNodeData(senderSockAddr, receivedPacket);
            }
        }
    }
}

void AssignmentClient::handleAuthenticationRequest() {
    const QString DATA_SERVER_USERNAME_ENV = "HIFI_AC_USERNAME";
    const QString DATA_SERVER_PASSWORD_ENV = "HIFI_AC_PASSWORD";
    
    // this node will be using an authentication server, let's make sure we have a username/password
    QProcessEnvironment sysEnvironment = QProcessEnvironment::systemEnvironment();
    
    QString username = sysEnvironment.value(DATA_SERVER_USERNAME_ENV);
    QString password = sysEnvironment.value(DATA_SERVER_PASSWORD_ENV);
    
    AccountManager& accountManager = AccountManager::getInstance();
    
    if (!username.isEmpty() && !password.isEmpty()) {
        // ask the account manager to log us in from the env variables
        accountManager.requestAccessToken(username, password);
    } else {
        qDebug() << "Authentication was requested against" << qPrintable(accountManager.getAuthURL().toString())
            << "but both or one of" << qPrintable(DATA_SERVER_USERNAME_ENV)
            << "/" << qPrintable(DATA_SERVER_PASSWORD_ENV) << "are not set. Unable to authenticate.";
        
        return;
    }
}

void AssignmentClient::assignmentCompleted() {
    // reset the logging target to the the CHILD_TARGET_NAME
    Logging::setTargetName(ASSIGNMENT_CLIENT_TARGET_NAME);
    
    qDebug("Assignment finished or never started - waiting for new assignment.");
    
    NodeList* nodeList = NodeList::getInstance();

    // have us handle incoming NodeList datagrams again
    disconnect(&nodeList->getNodeSocket(), 0, _currentAssignment.data(), 0);
    connect(&nodeList->getNodeSocket(), &QUdpSocket::readyRead, this, &AssignmentClient::readPendingDatagrams);
    
    // clear our current assignment shared pointer now that we're done with it
    // if the assignment thread is still around it has its own shared pointer to the assignment
    _currentAssignment.clear();

    // reset our NodeList by switching back to unassigned and clearing the list
    nodeList->setOwnerType(NodeType::Unassigned);
    nodeList->reset();
    nodeList->resetNodeInterestSet();
}
