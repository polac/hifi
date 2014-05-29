//
//  DomainServer.cpp
//  domain-server/src
//
//  Created by Stephen Birarda on 9/26/13.
//  Copyright 2013 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUrlQuery>

#include <AccountManager.h>
#include <HifiConfigVariantMap.h>
#include <HTTPConnection.h>
#include <PacketHeaders.h>
#include <SharedUtil.h>
#include <UUID.h>

#include "DomainServerNodeData.h"

#include "DomainServer.h"

DomainServer::DomainServer(int argc, char* argv[]) :
    QCoreApplication(argc, argv),
    _httpManager(DOMAIN_SERVER_HTTP_PORT, QString("%1/resources/web/").arg(QCoreApplication::applicationDirPath()), this),
    _httpsManager(NULL),
    _allAssignments(),
    _unfulfilledAssignments(),
    _pendingAssignedNodes(),
    _isUsingDTLS(false),
    _oauthProviderURL(),
    _oauthClientID(),
    _hostname(),
    _networkReplyUUIDMap(),
    _sessionAuthenticationHash()
{
    setOrganizationName("High Fidelity");
    setOrganizationDomain("highfidelity.io");
    setApplicationName("domain-server");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    
    _argumentVariantMap = HifiConfigVariantMap::mergeCLParametersWithJSONConfig(arguments());
    
    _networkAccessManager = new QNetworkAccessManager(this);
    
    if (optionallyReadX509KeyAndCertificate() && optionallySetupOAuth() && optionallySetupAssignmentPayment()) {
        // we either read a certificate and private key or were not passed one
        // and completed login or did not need to
        
        qDebug() << "Setting up LimitedNodeList and assignments.";
        setupNodeListAndAssignments();
    }
}

bool DomainServer::optionallyReadX509KeyAndCertificate() {
    const QString X509_CERTIFICATE_OPTION = "cert";
    const QString X509_PRIVATE_KEY_OPTION = "key";
    const QString X509_KEY_PASSPHRASE_ENV = "DOMAIN_SERVER_KEY_PASSPHRASE";
    
    QString certPath = _argumentVariantMap.value(X509_CERTIFICATE_OPTION).toString();
    QString keyPath = _argumentVariantMap.value(X509_PRIVATE_KEY_OPTION).toString();
    
    if (!certPath.isEmpty() && !keyPath.isEmpty()) {
        // the user wants to use DTLS to encrypt communication with nodes
        // let's make sure we can load the key and certificate
//        _x509Credentials = new gnutls_certificate_credentials_t;
//        gnutls_certificate_allocate_credentials(_x509Credentials);
        
        QString keyPassphraseString = QProcessEnvironment::systemEnvironment().value(X509_KEY_PASSPHRASE_ENV);
        
        qDebug() << "Reading certificate file at" << certPath << "for DTLS.";
        qDebug() << "Reading key file at" << keyPath << "for DTLS.";
        
//        int gnutlsReturn = gnutls_certificate_set_x509_key_file2(*_x509Credentials,
//                                                                 certPath.toLocal8Bit().constData(),
//                                                                 keyPath.toLocal8Bit().constData(),
//                                                                 GNUTLS_X509_FMT_PEM,
//                                                                 keyPassphraseString.toLocal8Bit().constData(),
//                                                                 0);
//        
//        if (gnutlsReturn < 0) {
//            qDebug() << "Unable to load certificate or key file." << "Error" << gnutlsReturn << "- domain-server will now quit.";
//            QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
//            return false;
//        }
        
//        qDebug() << "Successfully read certificate and private key.";
        
        // we need to also pass this certificate and private key to the HTTPS manager
        // this is used for Oauth callbacks when authorizing users against a data server
        
        QFile certFile(certPath);
        certFile.open(QIODevice::ReadOnly);
        
        QFile keyFile(keyPath);
        keyFile.open(QIODevice::ReadOnly);
        
        QSslCertificate sslCertificate(&certFile);
        QSslKey privateKey(&keyFile, QSsl::Rsa, QSsl::Pem, QSsl::PrivateKey, keyPassphraseString.toUtf8());
        
        _httpsManager = new HTTPSManager(DOMAIN_SERVER_HTTPS_PORT, sslCertificate, privateKey, QString(), this, this);
        
        qDebug() << "TCP server listening for HTTPS connections on" << DOMAIN_SERVER_HTTPS_PORT;
        
    } else if (!certPath.isEmpty() || !keyPath.isEmpty()) {
        qDebug() << "Missing certificate or private key. domain-server will now quit.";
        QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
        return false;
    }
    
    return true;
}

bool DomainServer::optionallySetupOAuth() {
    const QString OAUTH_PROVIDER_URL_OPTION = "oauth-provider";
    const QString OAUTH_CLIENT_ID_OPTION = "oauth-client-id";
    const QString OAUTH_CLIENT_SECRET_ENV = "DOMAIN_SERVER_CLIENT_SECRET";
    const QString REDIRECT_HOSTNAME_OPTION = "hostname";
    
    _oauthProviderURL = QUrl(_argumentVariantMap.value(OAUTH_PROVIDER_URL_OPTION).toString());
    _oauthClientID = _argumentVariantMap.value(OAUTH_CLIENT_ID_OPTION).toString();
    _oauthClientSecret = QProcessEnvironment::systemEnvironment().value(OAUTH_CLIENT_SECRET_ENV);
    _hostname = _argumentVariantMap.value(REDIRECT_HOSTNAME_OPTION).toString();
    
    if (!_oauthClientID.isEmpty()) {
        if (_oauthProviderURL.isEmpty()
            || _hostname.isEmpty()
            || _oauthClientID.isEmpty()
            || _oauthClientSecret.isEmpty()) {
            qDebug() << "Missing OAuth provider URL, hostname, client ID, or client secret. domain-server will now quit.";
            QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
            return false;
        } else {
            qDebug() << "OAuth will be used to identify clients using provider at" << _oauthProviderURL.toString();
            qDebug() << "OAuth Client ID is" << _oauthClientID;
        }
    }
    
    return true;
}

void DomainServer::setupNodeListAndAssignments(const QUuid& sessionUUID) {
    
    const QString CUSTOM_PORT_OPTION = "port";
    unsigned short domainServerPort = DEFAULT_DOMAIN_SERVER_PORT;
    
    if (_argumentVariantMap.contains(CUSTOM_PORT_OPTION)) {
        domainServerPort = (unsigned short) _argumentVariantMap.value(CUSTOM_PORT_OPTION).toUInt();
    }
    
    unsigned short domainServerDTLSPort = 0;
    
    if (_isUsingDTLS) {
        domainServerDTLSPort = DEFAULT_DOMAIN_SERVER_DTLS_PORT;
        
        const QString CUSTOM_DTLS_PORT_OPTION = "dtls-port";
        
        if (_argumentVariantMap.contains(CUSTOM_DTLS_PORT_OPTION)) {
            domainServerDTLSPort = (unsigned short) _argumentVariantMap.value(CUSTOM_DTLS_PORT_OPTION).toUInt();
        }
    }
    
    QSet<Assignment::Type> parsedTypes;
    parseAssignmentConfigs(parsedTypes);
    
    populateDefaultStaticAssignmentsExcludingTypes(parsedTypes);
    
    LimitedNodeList* nodeList = LimitedNodeList::createInstance(domainServerPort, domainServerDTLSPort);
    
    connect(nodeList, &LimitedNodeList::nodeAdded, this, &DomainServer::nodeAdded);
    connect(nodeList, &LimitedNodeList::nodeKilled, this, &DomainServer::nodeKilled);
    
    QTimer* silentNodeTimer = new QTimer(this);
    connect(silentNodeTimer, SIGNAL(timeout()), nodeList, SLOT(removeSilentNodes()));
    silentNodeTimer->start(NODE_SILENCE_THRESHOLD_MSECS);
    
    connect(&nodeList->getNodeSocket(), SIGNAL(readyRead()), SLOT(readAvailableDatagrams()));
    
    // add whatever static assignments that have been parsed to the queue
    addStaticAssignmentsToQueue();
}

bool DomainServer::optionallySetupAssignmentPayment() {
    // check if we have a username and password set via env
    const QString PAY_FOR_ASSIGNMENTS_OPTION = "pay-for-assignments";
    const QString HIFI_USERNAME_ENV_KEY = "DOMAIN_SERVER_USERNAME";
    const QString HIFI_PASSWORD_ENV_KEY = "DOMAIN_SERVER_PASSWORD";
    
    if (_argumentVariantMap.contains(PAY_FOR_ASSIGNMENTS_OPTION)) {
        if (!_oauthProviderURL.isEmpty()) {
            
            AccountManager& accountManager = AccountManager::getInstance();
            accountManager.setAuthURL(_oauthProviderURL);
            
            if (!accountManager.hasValidAccessToken()) {
                // we don't have a valid access token so we need to get one
                QString username = QProcessEnvironment::systemEnvironment().value(HIFI_USERNAME_ENV_KEY);
                QString password = QProcessEnvironment::systemEnvironment().value(HIFI_PASSWORD_ENV_KEY);
                
                if (!username.isEmpty() && !password.isEmpty()) {
                    accountManager.requestAccessToken(username, password);
                    
                    // connect to loginFailed signal from AccountManager so we can quit if that is the case
                    connect(&accountManager, &AccountManager::loginFailed, this, &DomainServer::loginFailed);
                } else {
                    qDebug() << "Missing access-token or username and password combination. domain-server will now quit.";
                    QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
                    return false;
                }
            }
            
            qDebug() << "Assignments will be paid for via" << qPrintable(_oauthProviderURL.toString());
            
            // assume that the fact we are authing against HF data server means we will pay for assignments
            // setup a timer to send transactions to pay assigned nodes every 30 seconds
            QTimer* creditSetupTimer = new QTimer(this);
            connect(creditSetupTimer, &QTimer::timeout, this, &DomainServer::setupPendingAssignmentCredits);
            
            const qint64 CREDIT_CHECK_INTERVAL_MSECS = 5 * 1000;
            creditSetupTimer->start(CREDIT_CHECK_INTERVAL_MSECS);
            
            QTimer* nodePaymentTimer = new QTimer(this);
            connect(nodePaymentTimer, &QTimer::timeout, this, &DomainServer::sendPendingTransactionsToServer);
            
            const qint64 TRANSACTION_SEND_INTERVAL_MSECS = 30 * 1000;
            nodePaymentTimer->start(TRANSACTION_SEND_INTERVAL_MSECS);
            
        } else {
            qDebug() << "Missing OAuth provider URL, but assigned node payment was enabled. domain-server will now quit.";
            QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
            
            return false;
        }
    }
    
    return true;
}

void DomainServer::loginFailed() {
    qDebug() << "Login to data server has failed. domain-server will now quit";
    QMetaObject::invokeMethod(this, "quit", Qt::QueuedConnection);
}

void DomainServer::parseAssignmentConfigs(QSet<Assignment::Type>& excludedTypes) {
    // check for configs from the command line, these take precedence
    const QString ASSIGNMENT_CONFIG_REGEX_STRING = "config-([\\d]+)";
    QRegExp assignmentConfigRegex(ASSIGNMENT_CONFIG_REGEX_STRING);
    
    // scan for assignment config keys
    QStringList variantMapKeys = _argumentVariantMap.keys();
    int configIndex = variantMapKeys.indexOf(assignmentConfigRegex);
    
    while (configIndex != -1) {
        // figure out which assignment type this matches
        Assignment::Type assignmentType = (Assignment::Type) assignmentConfigRegex.cap(1).toInt();
        
        if (assignmentType < Assignment::AllTypes && !excludedTypes.contains(assignmentType)) {
            QVariant mapValue = _argumentVariantMap[variantMapKeys[configIndex]];
            QJsonArray assignmentArray;
            
            if (mapValue.type() == QVariant::String) {
                QJsonDocument deserializedDocument = QJsonDocument::fromJson(mapValue.toString().toUtf8());
                assignmentArray = deserializedDocument.array();
            } else {
                assignmentArray = mapValue.toJsonValue().toArray();
            }
            
            if (assignmentType != Assignment::AgentType) {
                createStaticAssignmentsForType(assignmentType, assignmentArray);
            } else {
                createScriptedAssignmentsFromArray(assignmentArray);
            }
            
            excludedTypes.insert(assignmentType);
        }
        
        configIndex = variantMapKeys.indexOf(assignmentConfigRegex, configIndex + 1);
    }
}

void DomainServer::addStaticAssignmentToAssignmentHash(Assignment* newAssignment) {
    qDebug() << "Inserting assignment" << *newAssignment << "to static assignment hash.";
    newAssignment->setIsStatic(true);
    _allAssignments.insert(newAssignment->getUUID(), SharedAssignmentPointer(newAssignment));
}

void DomainServer::createScriptedAssignmentsFromArray(const QJsonArray &configArray) {    
    foreach(const QJsonValue& jsonValue, configArray) {
        if (jsonValue.isObject()) {
            QJsonObject jsonObject = jsonValue.toObject();
            
            // make sure we were passed a URL, otherwise this is an invalid scripted assignment
            const QString  ASSIGNMENT_URL_KEY = "url";
            QString assignmentURL = jsonObject[ASSIGNMENT_URL_KEY].toString();
            
            if (!assignmentURL.isEmpty()) {
                // check the json for a pool
                const QString ASSIGNMENT_POOL_KEY = "pool";
                QString assignmentPool = jsonObject[ASSIGNMENT_POOL_KEY].toString();
                
                // check for a number of instances, if not passed then default is 1
                const QString ASSIGNMENT_INSTANCES_KEY = "instances";
                int numInstances = jsonObject[ASSIGNMENT_INSTANCES_KEY].toInt();
                numInstances = (numInstances == 0 ? 1 : numInstances);
                
                qDebug() << "Adding a static scripted assignment from" << assignmentURL;
                
                for (int i = 0; i < numInstances; i++) {
                    // add a scripted assignment to the queue for this instance
                    Assignment* scriptAssignment = new Assignment(Assignment::CreateCommand,
                                                                  Assignment::AgentType,
                                                                  assignmentPool);
                    scriptAssignment->setPayload(assignmentURL.toUtf8());
                    
                    // scripts passed on CL or via JSON are static - so they are added back to the queue if the node dies
                    addStaticAssignmentToAssignmentHash(scriptAssignment);
                }
            }
        }
    }
}

void DomainServer::createStaticAssignmentsForType(Assignment::Type type, const QJsonArray& configArray) {
    // we have a string for config for this type
    qDebug() << "Parsing config for assignment type" << type;
    
    int configCounter = 0;
    
    foreach(const QJsonValue& jsonValue, configArray) {
        if (jsonValue.isObject()) {
            QJsonObject jsonObject = jsonValue.toObject();
            
            // check the config string for a pool
            const QString ASSIGNMENT_POOL_KEY = "pool";
            QString assignmentPool;
            
            QJsonValue poolValue = jsonObject[ASSIGNMENT_POOL_KEY];
            if (!poolValue.isUndefined()) {
                assignmentPool = poolValue.toString();
                
                jsonObject.remove(ASSIGNMENT_POOL_KEY);
            }
            
            ++configCounter;
            qDebug() << "Type" << type << "config" << configCounter << "=" << jsonObject;
            
            Assignment* configAssignment = new Assignment(Assignment::CreateCommand, type, assignmentPool);
            
            // setup the payload as a semi-colon separated list of key = value
            QStringList payloadStringList;
            foreach(const QString& payloadKey, jsonObject.keys()) {
                QString dashes = payloadKey.size() == 1 ? "-" : "--";
                payloadStringList << QString("%1%2 %3").arg(dashes).arg(payloadKey).arg(jsonObject[payloadKey].toString());
            }
            
            configAssignment->setPayload(payloadStringList.join(' ').toUtf8());
            
            addStaticAssignmentToAssignmentHash(configAssignment);
        }
    }
}

void DomainServer::populateDefaultStaticAssignmentsExcludingTypes(const QSet<Assignment::Type>& excludedTypes) {
    // enumerate over all assignment types and see if we've already excluded it
    for (Assignment::Type defaultedType = Assignment::AudioMixerType;
         defaultedType != Assignment::AllTypes;
         defaultedType =  static_cast<Assignment::Type>(static_cast<int>(defaultedType) + 1)) {
        if (!excludedTypes.contains(defaultedType) && defaultedType != Assignment::AgentType) {
            // type has not been set from a command line or config file config, use the default
            // by clearing whatever exists and writing a single default assignment with no payload
            Assignment* newAssignment = new Assignment(Assignment::CreateCommand, (Assignment::Type) defaultedType);
            addStaticAssignmentToAssignmentHash(newAssignment);
        }
    }
}

const QString ALLOWED_ROLES_CONFIG_KEY = "allowed-roles";

const NodeSet STATICALLY_ASSIGNED_NODES = NodeSet() << NodeType::AudioMixer
    << NodeType::AvatarMixer << NodeType::VoxelServer << NodeType::ParticleServer << NodeType::ModelServer
    << NodeType::MetavoxelServer;

void DomainServer::handleConnectRequest(const QByteArray& packet, const HifiSockAddr& senderSockAddr) {

    NodeType_t nodeType;
    HifiSockAddr publicSockAddr, localSockAddr;
    
    int numPreInterestBytes = parseNodeDataFromByteArray(nodeType, publicSockAddr, localSockAddr, packet, senderSockAddr);
    
    QUuid packetUUID = uuidFromPacketHeader(packet);

    // check if this connect request matches an assignment in the queue
    bool isAssignment = _pendingAssignedNodes.contains(packetUUID);
    SharedAssignmentPointer matchingQueuedAssignment = SharedAssignmentPointer();
    PendingAssignedNodeData* pendingAssigneeData = NULL;
   
    if (isAssignment) {
        pendingAssigneeData = _pendingAssignedNodes.value(packetUUID);
        
        if (pendingAssigneeData) {
            matchingQueuedAssignment = matchingQueuedAssignmentForCheckIn(pendingAssigneeData->getAssignmentUUID(), nodeType);
            
            if (matchingQueuedAssignment) {
                qDebug() << "Assignment deployed with" << uuidStringWithoutCurlyBraces(packetUUID)
                    << "matches unfulfilled assignment"
                    << uuidStringWithoutCurlyBraces(matchingQueuedAssignment->getUUID());
                
                // remove this unique assignment deployment from the hash of pending assigned nodes
                // cleanup of the PendingAssignedNodeData happens below after the node has been added to the LimitedNodeList
                _pendingAssignedNodes.remove(packetUUID);
            } else {
                // this is a node connecting to fulfill an assignment that doesn't exist
                // don't reply back to them so they cycle back and re-request an assignment
                qDebug() << "No match for assignment deployed with" << uuidStringWithoutCurlyBraces(packetUUID);
                return;
            }
        }
        
    }
    
    if (!isAssignment && !_oauthProviderURL.isEmpty() && _argumentVariantMap.contains(ALLOWED_ROLES_CONFIG_KEY)) {
        // this is an Agent, and we require authentication so we can compare the user's roles to our list of allowed ones
        if (_sessionAuthenticationHash.contains(packetUUID)) {
            if (!_sessionAuthenticationHash.value(packetUUID)) {
                // we've decided this is a user that isn't allowed in, return out
                // TODO: provide information to the user so they know why they can't connect
                return;
            } else {
                // we're letting this user in, don't return and remove their UUID from the hash
                _sessionAuthenticationHash.remove(packetUUID);
            }
        } else {
            // we don't know anything about this client
            // we have an OAuth provider, ask this interface client to auth against it
            QByteArray oauthRequestByteArray = byteArrayWithPopulatedHeader(PacketTypeDomainOAuthRequest);
            QDataStream oauthRequestStream(&oauthRequestByteArray, QIODevice::Append);
            QUrl authorizationURL = packetUUID.isNull() ? oauthAuthorizationURL() : oauthAuthorizationURL(packetUUID);
            oauthRequestStream << authorizationURL;
            
            // send this oauth request datagram back to the client
            LimitedNodeList::getInstance()->writeUnverifiedDatagram(oauthRequestByteArray, senderSockAddr);
            
            return;
        }
    }
        
    if ((!isAssignment && !STATICALLY_ASSIGNED_NODES.contains(nodeType))
        || (isAssignment && matchingQueuedAssignment)) {
        // this was either not a static assignment or it was and we had a matching one in the queue
        
        // create a new session UUID for this node
        QUuid nodeUUID = QUuid::createUuid();
        
        SharedNodePointer newNode = LimitedNodeList::getInstance()->addOrUpdateNode(nodeUUID, nodeType,
                                                                                    publicSockAddr, localSockAddr);
        // when the newNode is created the linked data is also created
        // if this was a static assignment set the UUID, set the sendingSockAddr
        DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(newNode->getLinkedData());
        
        if (isAssignment) {
            nodeData->setAssignmentUUID(matchingQueuedAssignment->getUUID());
            nodeData->setWalletUUID(pendingAssigneeData->getWalletUUID());
            
            // now that we've pulled the wallet UUID and added the node to our list, delete the pending assignee data
            delete pendingAssigneeData;
        }
        
        nodeData->setSendingSockAddr(senderSockAddr);
        
        // reply back to the user with a PacketTypeDomainList
        sendDomainListToNode(newNode, senderSockAddr, nodeInterestListFromPacket(packet, numPreInterestBytes));
    }
}

QUrl DomainServer::oauthRedirectURL() {
    return QString("https://%1:%2/oauth").arg(_hostname).arg(_httpsManager->serverPort());
}

const QString OAUTH_CLIENT_ID_QUERY_KEY = "client_id";
const QString OAUTH_REDIRECT_URI_QUERY_KEY = "redirect_uri";

QUrl DomainServer::oauthAuthorizationURL(const QUuid& stateUUID) {
    // for now these are all interface clients that have a GUI
    // so just send them back the full authorization URL
    QUrl authorizationURL = _oauthProviderURL;
    
    const QString OAUTH_AUTHORIZATION_PATH = "/oauth/authorize";
    authorizationURL.setPath(OAUTH_AUTHORIZATION_PATH);
    
    QUrlQuery authorizationQuery;
    
    authorizationQuery.addQueryItem(OAUTH_CLIENT_ID_QUERY_KEY, _oauthClientID);
    
    const QString OAUTH_RESPONSE_TYPE_QUERY_KEY = "response_type";
    const QString OAUTH_REPSONSE_TYPE_QUERY_VALUE = "code";
    authorizationQuery.addQueryItem(OAUTH_RESPONSE_TYPE_QUERY_KEY, OAUTH_REPSONSE_TYPE_QUERY_VALUE);
    
    const QString OAUTH_STATE_QUERY_KEY = "state";
    // create a new UUID that will be the state parameter for oauth authorization AND the new session UUID for that node
    authorizationQuery.addQueryItem(OAUTH_STATE_QUERY_KEY, uuidStringWithoutCurlyBraces(stateUUID));
    
    authorizationQuery.addQueryItem(OAUTH_REDIRECT_URI_QUERY_KEY, oauthRedirectURL().toString());
    
    authorizationURL.setQuery(authorizationQuery);
    
    return authorizationURL;
}

int DomainServer::parseNodeDataFromByteArray(NodeType_t& nodeType, HifiSockAddr& publicSockAddr,
                                              HifiSockAddr& localSockAddr, const QByteArray& packet,
                                              const HifiSockAddr& senderSockAddr) {
    QDataStream packetStream(packet);
    packetStream.skipRawData(numBytesForPacketHeader(packet));
    
    packetStream >> nodeType;
    packetStream >> publicSockAddr >> localSockAddr;
    
    if (publicSockAddr.getAddress().isNull()) {
        // this node wants to use us its STUN server
        // so set the node public address to whatever we perceive the public address to be
        
        // if the sender is on our box then leave its public address to 0 so that
        // other users attempt to reach it on the same address they have for the domain-server
        if (senderSockAddr.getAddress().isLoopback()) {
            publicSockAddr.setAddress(QHostAddress());
        } else {
            publicSockAddr.setAddress(senderSockAddr.getAddress());
        }
    }
    
    return packetStream.device()->pos();
}

NodeSet DomainServer::nodeInterestListFromPacket(const QByteArray& packet, int numPreceedingBytes) {
    QDataStream packetStream(packet);
    packetStream.skipRawData(numPreceedingBytes);
    
    quint8 numInterestTypes = 0;
    packetStream >> numInterestTypes;
    
    quint8 nodeType;
    NodeSet nodeInterestSet;
    
    for (int i = 0; i < numInterestTypes; i++) {
        packetStream >> nodeType;
        nodeInterestSet.insert((NodeType_t) nodeType);
    }
    
    return nodeInterestSet;
}

void DomainServer::sendDomainListToNode(const SharedNodePointer& node, const HifiSockAddr &senderSockAddr,
                                        const NodeSet& nodeInterestList) {
    
    QByteArray broadcastPacket = byteArrayWithPopulatedHeader(PacketTypeDomainList);
    
    // always send the node their own UUID back
    QDataStream broadcastDataStream(&broadcastPacket, QIODevice::Append);
    broadcastDataStream << node->getUUID();
    
    int numBroadcastPacketLeadBytes = broadcastDataStream.device()->pos();
    
    DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
    
    LimitedNodeList* nodeList = LimitedNodeList::getInstance();
    
    if (nodeInterestList.size() > 0) {
        
//        DTLSServerSession* dtlsSession = _isUsingDTLS ? _dtlsSessions[senderSockAddr] : NULL;
        int dataMTU = MAX_PACKET_SIZE;
        
        if (nodeData->isAuthenticated()) {
            // if this authenticated node has any interest types, send back those nodes as well
            foreach (const SharedNodePointer& otherNode, nodeList->getNodeHash()) {
                
                // reset our nodeByteArray and nodeDataStream
                QByteArray nodeByteArray;
                QDataStream nodeDataStream(&nodeByteArray, QIODevice::Append);
                
                if (otherNode->getUUID() != node->getUUID() && nodeInterestList.contains(otherNode->getType())) {
                    
                    // don't send avatar nodes to other avatars, that will come from avatar mixer
                    nodeDataStream << *otherNode.data();
                    
                    // pack the secret that these two nodes will use to communicate with each other
                    QUuid secretUUID = nodeData->getSessionSecretHash().value(otherNode->getUUID());
                    if (secretUUID.isNull()) {
                        // generate a new secret UUID these two nodes can use
                        secretUUID = QUuid::createUuid();
                        
                        // set that on the current Node's sessionSecretHash
                        nodeData->getSessionSecretHash().insert(otherNode->getUUID(), secretUUID);
                        
                        // set it on the other Node's sessionSecretHash
                        reinterpret_cast<DomainServerNodeData*>(otherNode->getLinkedData())
                        ->getSessionSecretHash().insert(node->getUUID(), secretUUID);
                        
                    }
                    
                    nodeDataStream << secretUUID;
                    
                    if (broadcastPacket.size() +  nodeByteArray.size() > dataMTU) {
                        // we need to break here and start a new packet
                        // so send the current one
                        
                        nodeList->writeDatagram(broadcastPacket, node, senderSockAddr);
                        
                        // reset the broadcastPacket structure
                        broadcastPacket.resize(numBroadcastPacketLeadBytes);
                        broadcastDataStream.device()->seek(numBroadcastPacketLeadBytes);
                    }
                    
                    // append the nodeByteArray to the current state of broadcastDataStream
                    broadcastPacket.append(nodeByteArray);
                }
            }
        }
        
        // always write the last broadcastPacket
        nodeList->writeDatagram(broadcastPacket, node, senderSockAddr);
    }
}

void DomainServer::readAvailableDatagrams() {
    LimitedNodeList* nodeList = LimitedNodeList::getInstance();

    HifiSockAddr senderSockAddr;
    QByteArray receivedPacket;
    
    static QByteArray assignmentPacket = byteArrayWithPopulatedHeader(PacketTypeCreateAssignment);
    static int numAssignmentPacketHeaderBytes = assignmentPacket.size();

    while (nodeList->getNodeSocket().hasPendingDatagrams()) {
        receivedPacket.resize(nodeList->getNodeSocket().pendingDatagramSize());
        nodeList->getNodeSocket().readDatagram(receivedPacket.data(), receivedPacket.size(),
                                               senderSockAddr.getAddressPointer(), senderSockAddr.getPortPointer());
        if (packetTypeForPacket(receivedPacket) == PacketTypeRequestAssignment
            && nodeList->packetVersionAndHashMatch(receivedPacket)) {

            // construct the requested assignment from the packet data
            Assignment requestAssignment(receivedPacket);
            
            // Suppress these for Assignment::AgentType to once per 5 seconds
            static QElapsedTimer noisyMessageTimer;
            static bool wasNoisyTimerStarted = false;
            
            if (!wasNoisyTimerStarted) {
                noisyMessageTimer.start();
                wasNoisyTimerStarted = true;
            }
            
            const quint64 NOISY_MESSAGE_INTERVAL_MSECS = 5 * 1000;
        
            if (requestAssignment.getType() != Assignment::AgentType
                || noisyMessageTimer.elapsed() > NOISY_MESSAGE_INTERVAL_MSECS) {
                qDebug() << "Received a request for assignment type" << requestAssignment.getType()
                    << "from" << senderSockAddr;
                noisyMessageTimer.restart();
            }
            
            SharedAssignmentPointer assignmentToDeploy = deployableAssignmentForRequest(requestAssignment);
            
            if (assignmentToDeploy) {
                qDebug() << "Deploying assignment -" << *assignmentToDeploy.data() << "- to" << senderSockAddr;
                
                // give this assignment out, either the type matches or the requestor said they will take any
                assignmentPacket.resize(numAssignmentPacketHeaderBytes);
                
                // setup a copy of this assignment that will have a unique UUID, for packaging purposes
                Assignment uniqueAssignment(*assignmentToDeploy.data());
                uniqueAssignment.setUUID(QUuid::createUuid());
                
                QDataStream assignmentStream(&assignmentPacket, QIODevice::Append);
                
                assignmentStream << uniqueAssignment;
                
                nodeList->getNodeSocket().writeDatagram(assignmentPacket,
                                                        senderSockAddr.getAddress(), senderSockAddr.getPort());
                
                // add the information for that deployed assignment to the hash of pending assigned nodes
                PendingAssignedNodeData* pendingNodeData = new PendingAssignedNodeData(assignmentToDeploy->getUUID(),
                                                                                       requestAssignment.getWalletUUID());
                _pendingAssignedNodes.insert(uniqueAssignment.getUUID(), pendingNodeData);
            } else {
                if (requestAssignment.getType() != Assignment::AgentType
                    || noisyMessageTimer.elapsed() > NOISY_MESSAGE_INTERVAL_MSECS) {
                    qDebug() << "Unable to fulfill assignment request of type" << requestAssignment.getType()
                        << "from" << senderSockAddr;
                    noisyMessageTimer.restart();
                }
            }
        } else if (!_isUsingDTLS) {
            // not using DTLS, process datagram normally
            processDatagram(receivedPacket, senderSockAddr);
        } else {
            // we're using DTLS, so tell the sender to get back to us using DTLS
            static QByteArray dtlsRequiredPacket = byteArrayWithPopulatedHeader(PacketTypeDomainServerRequireDTLS);
            static int numBytesDTLSHeader = numBytesForPacketHeaderGivenPacketType(PacketTypeDomainServerRequireDTLS);
            
            if (dtlsRequiredPacket.size() == numBytesDTLSHeader) {
                // pack the port that we accept DTLS traffic on
                unsigned short dtlsPort = nodeList->getDTLSSocket().localPort();
                dtlsRequiredPacket.replace(numBytesDTLSHeader, sizeof(dtlsPort), reinterpret_cast<const char*>(&dtlsPort));
            }

            nodeList->writeUnverifiedDatagram(dtlsRequiredPacket, senderSockAddr);
        }
    }
}

void DomainServer::setupPendingAssignmentCredits() {
    // enumerate the NodeList to find the assigned nodes
    foreach (const SharedNodePointer& node, LimitedNodeList::getInstance()->getNodeHash()) {
        DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
        
        if (!nodeData->getAssignmentUUID().isNull() && !nodeData->getWalletUUID().isNull()) {
            // check if we have a non-finalized transaction for this node to add this amount to
            TransactionHash::iterator i = _pendingAssignmentCredits.find(nodeData->getWalletUUID());
            WalletTransaction* existingTransaction = NULL;
            
            while (i != _pendingAssignmentCredits.end() && i.key() == nodeData->getWalletUUID()) {
                if (!i.value()->isFinalized()) {
                    existingTransaction = i.value();
                    break;
                } else {
                    ++i;
                }
            }
            
            qint64 elapsedMsecsSinceLastPayment = nodeData->getPaymentIntervalTimer().elapsed();
            nodeData->getPaymentIntervalTimer().restart();
            
            const float CREDITS_PER_HOUR = 0.10f;
            const float CREDITS_PER_MSEC = CREDITS_PER_HOUR / (60 * 60 * 1000);
            const int SATOSHIS_PER_MSEC = CREDITS_PER_MSEC * SATOSHIS_PER_CREDIT;
    
            float pendingCredits = elapsedMsecsSinceLastPayment * SATOSHIS_PER_MSEC;
            
            if (existingTransaction) {
                existingTransaction->incrementAmount(pendingCredits);
            } else {
                // create a fresh transaction to pay this node, there is no transaction to append to
                WalletTransaction* freshTransaction = new WalletTransaction(nodeData->getWalletUUID(), pendingCredits);
                _pendingAssignmentCredits.insert(nodeData->getWalletUUID(), freshTransaction);
            }
        }
    }
}

void DomainServer::sendPendingTransactionsToServer() {
    
    AccountManager& accountManager = AccountManager::getInstance();
    
    if (accountManager.hasValidAccessToken()) {
        
        // enumerate the pending transactions and send them to the server to complete payment
        TransactionHash::iterator i = _pendingAssignmentCredits.begin();
        
        JSONCallbackParameters transactionCallbackParams;
        
        transactionCallbackParams.jsonCallbackReceiver = this;
        transactionCallbackParams.jsonCallbackMethod = "transactionJSONCallback";
        
        while (i != _pendingAssignmentCredits.end()) {
            accountManager.authenticatedRequest("api/v1/transactions", QNetworkAccessManager::PostOperation,
                                                transactionCallbackParams, i.value()->postJson().toJson());
            
            // set this transaction to finalized so we don't add additional credits to it
            i.value()->setIsFinalized(true);
            
            ++i;
        }
    }
    
}

void DomainServer::transactionJSONCallback(const QJsonObject& data) {
    // check if this was successful - if so we can remove it from our list of pending
    if (data.value("status").toString() == "success") {
        // create a dummy wallet transaction to unpack the JSON to
        WalletTransaction dummyTransaction;
        dummyTransaction.loadFromJson(data);
        
        TransactionHash::iterator i = _pendingAssignmentCredits.find(dummyTransaction.getDestinationUUID());
        
        while (i != _pendingAssignmentCredits.end() && i.key() == dummyTransaction.getDestinationUUID()) {
            if (i.value()->getUUID() == dummyTransaction.getUUID()) {
                // we have a match - we can remove this from the hash of pending credits
                // and delete it for clean up
                
                WalletTransaction* matchingTransaction = i.value();
                _pendingAssignmentCredits.erase(i);
                delete matchingTransaction;
                
                break;
            } else {
                ++i;
            }
        }
    }
}

void DomainServer::processDatagram(const QByteArray& receivedPacket, const HifiSockAddr& senderSockAddr) {
    LimitedNodeList* nodeList = LimitedNodeList::getInstance();
    
    if (nodeList->packetVersionAndHashMatch(receivedPacket)) {
        PacketType requestType = packetTypeForPacket(receivedPacket);
        
        if (requestType == PacketTypeDomainConnectRequest) {
            handleConnectRequest(receivedPacket, senderSockAddr);
        } else if (requestType == PacketTypeDomainListRequest) {
            QUuid nodeUUID = uuidFromPacketHeader(receivedPacket);
            
            if (!nodeUUID.isNull() && nodeList->nodeWithUUID(nodeUUID)) {
                NodeType_t throwawayNodeType;
                HifiSockAddr nodePublicAddress, nodeLocalAddress;
                
                int numNodeInfoBytes = parseNodeDataFromByteArray(throwawayNodeType, nodePublicAddress, nodeLocalAddress,
                                                                  receivedPacket, senderSockAddr);
                
                SharedNodePointer checkInNode = nodeList->updateSocketsForNode(nodeUUID, nodePublicAddress, nodeLocalAddress);
                
                // update last receive to now
                quint64 timeNow = usecTimestampNow();
                checkInNode->setLastHeardMicrostamp(timeNow);
            
                sendDomainListToNode(checkInNode, senderSockAddr, nodeInterestListFromPacket(receivedPacket, numNodeInfoBytes));
            }
        } else if (requestType == PacketTypeNodeJsonStats) {
            SharedNodePointer matchingNode = nodeList->sendingNodeForPacket(receivedPacket);
            if (matchingNode) {
                reinterpret_cast<DomainServerNodeData*>(matchingNode->getLinkedData())->parseJSONStatsPacket(receivedPacket);
            }
        }
    }
}

QJsonObject DomainServer::jsonForSocket(const HifiSockAddr& socket) {
    QJsonObject socketJSON;

    socketJSON["ip"] = socket.getAddress().toString();
    socketJSON["port"] = ntohs(socket.getPort());

    return socketJSON;
}

const char JSON_KEY_UUID[] = "uuid";
const char JSON_KEY_TYPE[] = "type";
const char JSON_KEY_PUBLIC_SOCKET[] = "public";
const char JSON_KEY_LOCAL_SOCKET[] = "local";
const char JSON_KEY_POOL[] = "pool";
const char JSON_KEY_PENDING_CREDITS[] = "pending_credits";
const char JSON_KEY_WAKE_TIMESTAMP[] = "wake_timestamp";

QJsonObject DomainServer::jsonObjectForNode(const SharedNodePointer& node) {
    QJsonObject nodeJson;

    // re-format the type name so it matches the target name
    QString nodeTypeName = NodeType::getNodeTypeName(node->getType());
    nodeTypeName = nodeTypeName.toLower();
    nodeTypeName.replace(' ', '-');

    // add the node UUID
    nodeJson[JSON_KEY_UUID] = uuidStringWithoutCurlyBraces(node->getUUID());
    
    // add the node type
    nodeJson[JSON_KEY_TYPE] = nodeTypeName;

    // add the node socket information
    nodeJson[JSON_KEY_PUBLIC_SOCKET] = jsonForSocket(node->getPublicSocket());
    nodeJson[JSON_KEY_LOCAL_SOCKET] = jsonForSocket(node->getLocalSocket());
    
    // add the node uptime in our list
    nodeJson[JSON_KEY_WAKE_TIMESTAMP] = QString::number(node->getWakeTimestamp());
    
    // if the node has pool information, add it
    DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
    SharedAssignmentPointer matchingAssignment = _allAssignments.value(nodeData->getAssignmentUUID());
    if (matchingAssignment) {
        nodeJson[JSON_KEY_POOL] = matchingAssignment->getPool();
        
        if (!nodeData->getWalletUUID().isNull()) {
            TransactionHash::iterator i = _pendingAssignmentCredits.find(nodeData->getWalletUUID());
            float pendingCreditAmount = 0;
            
            while (i != _pendingAssignmentCredits.end() && i.key() == nodeData->getWalletUUID()) {
                pendingCreditAmount += i.value()->getAmount() / SATOSHIS_PER_CREDIT;
                ++i;
            }
            
            nodeJson[JSON_KEY_PENDING_CREDITS] = pendingCreditAmount;
        }
    }
    
    return nodeJson;
}

const char ASSIGNMENT_SCRIPT_HOST_LOCATION[] = "resources/web/assignment";

QString pathForAssignmentScript(const QUuid& assignmentUUID) {
    QString newPath(ASSIGNMENT_SCRIPT_HOST_LOCATION);
    newPath += "/";
    // append the UUID for this script as the new filename, remove the curly braces
    newPath += uuidStringWithoutCurlyBraces(assignmentUUID);
    return newPath;
}

bool DomainServer::handleHTTPRequest(HTTPConnection* connection, const QUrl& url) {
    const QString JSON_MIME_TYPE = "application/json";
    
    const QString URI_ASSIGNMENT = "/assignment";
    const QString URI_NODES = "/nodes";
    
    const QString UUID_REGEX_STRING = "[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}";
    
    if (connection->requestOperation() == QNetworkAccessManager::GetOperation) {
        if (url.path() == "/assignments.json") {
            // user is asking for json list of assignments
            
            // setup the JSON
            QJsonObject assignmentJSON;
            QJsonObject assignedNodesJSON;
            
            // enumerate the NodeList to find the assigned nodes
            foreach (const SharedNodePointer& node, LimitedNodeList::getInstance()->getNodeHash()) {
                DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
                
                if (!nodeData->getAssignmentUUID().isNull()) {
                    // add the node using the UUID as the key
                    QString uuidString = uuidStringWithoutCurlyBraces(nodeData->getAssignmentUUID());
                    assignedNodesJSON[uuidString] = jsonObjectForNode(node);
                }
            }
            
            assignmentJSON["fulfilled"] = assignedNodesJSON;
            
            QJsonObject queuedAssignmentsJSON;
            
            // add the queued but unfilled assignments to the json
            foreach(const SharedAssignmentPointer& assignment, _unfulfilledAssignments) {
                QJsonObject queuedAssignmentJSON;
                
                QString uuidString = uuidStringWithoutCurlyBraces(assignment->getUUID());
                queuedAssignmentJSON[JSON_KEY_TYPE] = QString(assignment->getTypeName());
                
                // if the assignment has a pool, add it
                if (!assignment->getPool().isEmpty()) {
                    queuedAssignmentJSON[JSON_KEY_POOL] = assignment->getPool();
                }
                
                // add this queued assignment to the JSON
                queuedAssignmentsJSON[uuidString] = queuedAssignmentJSON;
            }
            
            assignmentJSON["queued"] = queuedAssignmentsJSON;
            
            // print out the created JSON
            QJsonDocument assignmentDocument(assignmentJSON);
            connection->respond(HTTPConnection::StatusCode200, assignmentDocument.toJson(), qPrintable(JSON_MIME_TYPE));
            
            // we've processed this request
            return true;
        } else if (url.path() == "/transactions.json") {
            // enumerate our pending transactions and display them in an array
            QJsonObject rootObject;
            QJsonArray transactionArray;
            
            TransactionHash::iterator i = _pendingAssignmentCredits.begin();
            while (i != _pendingAssignmentCredits.end()) {
                transactionArray.push_back(i.value()->toJson());
                ++i;
            }
            
            rootObject["pending_transactions"] = transactionArray;
            
            // print out the created JSON
            QJsonDocument transactionsDocument(rootObject);
            connection->respond(HTTPConnection::StatusCode200, transactionsDocument.toJson(), qPrintable(JSON_MIME_TYPE));
            
            return true;
        } else if (url.path() == QString("%1.json").arg(URI_NODES)) {
            // setup the JSON
            QJsonObject rootJSON;
            QJsonArray nodesJSONArray;
            
            // enumerate the NodeList to find the assigned nodes
            LimitedNodeList* nodeList = LimitedNodeList::getInstance();
            
            foreach (const SharedNodePointer& node, nodeList->getNodeHash()) {
                // add the node using the UUID as the key
                nodesJSONArray.append(jsonObjectForNode(node));
            }
            
            rootJSON["nodes"] = nodesJSONArray;
            
            // print out the created JSON
            QJsonDocument nodesDocument(rootJSON);
            
            // send the response
            connection->respond(HTTPConnection::StatusCode200, nodesDocument.toJson(), qPrintable(JSON_MIME_TYPE));
            
            return true;
        } else {
            // check if this is for json stats for a node
            const QString NODE_JSON_REGEX_STRING = QString("\\%1\\/(%2).json\\/?$").arg(URI_NODES).arg(UUID_REGEX_STRING);
            QRegExp nodeShowRegex(NODE_JSON_REGEX_STRING);
            
            if (nodeShowRegex.indexIn(url.path()) != -1) {
                QUuid matchingUUID = QUuid(nodeShowRegex.cap(1));
                
                // see if we have a node that matches this ID
                SharedNodePointer matchingNode = LimitedNodeList::getInstance()->nodeWithUUID(matchingUUID);
                if (matchingNode) {
                    // create a QJsonDocument with the stats QJsonObject
                    QJsonObject statsObject =
                        reinterpret_cast<DomainServerNodeData*>(matchingNode->getLinkedData())->getStatsJSONObject();
                    
                    // add the node type to the JSON data for output purposes
                    statsObject["node_type"] = NodeType::getNodeTypeName(matchingNode->getType()).toLower().replace(' ', '-');
                    
                    QJsonDocument statsDocument(statsObject);
                    
                    // send the response
                    connection->respond(HTTPConnection::StatusCode200, statsDocument.toJson(), qPrintable(JSON_MIME_TYPE));
                    
                    // tell the caller we processed the request
                    return true;
                }
                
                return false;
            }
            
            // check if this is a request for a scripted assignment (with a temp unique UUID)
            const QString  ASSIGNMENT_REGEX_STRING = QString("\\%1\\/(%2)\\/?$").arg(URI_ASSIGNMENT).arg(UUID_REGEX_STRING);
            QRegExp assignmentRegex(ASSIGNMENT_REGEX_STRING);
        
            if (assignmentRegex.indexIn(url.path()) != -1) {
                QUuid matchingUUID = QUuid(assignmentRegex.cap(1));
                
                SharedAssignmentPointer matchingAssignment = _allAssignments.value(matchingUUID);
                if (!matchingAssignment) {
                    // check if we have a pending assignment that matches this temp UUID, and it is a scripted assignment
                    PendingAssignedNodeData* pendingData = _pendingAssignedNodes.value(matchingUUID);
                    if (pendingData) {
                        matchingAssignment = _allAssignments.value(pendingData->getAssignmentUUID());
                        
                        if (matchingAssignment && matchingAssignment->getType() == Assignment::AgentType) {
                            // we have a matching assignment and it is for the right type, have the HTTP manager handle it
                            // via correct URL for the script so the client can download
                            
                            QUrl scriptURL = url;
                            scriptURL.setPath(URI_ASSIGNMENT + "/"
                                              + uuidStringWithoutCurlyBraces(pendingData->getAssignmentUUID()));
                            
                            // have the HTTPManager serve the appropriate script file
                            return _httpManager.handleHTTPRequest(connection, scriptURL);
                        }
                    }
                }
                
                // request not handled
                return false;
            }
        }
    } else if (connection->requestOperation() == QNetworkAccessManager::PostOperation) {
        if (url.path() == URI_ASSIGNMENT) {
            // this is a script upload - ask the HTTPConnection to parse the form data
            QList<FormData> formData = connection->parseFormData();
            
            // check optional headers for # of instances and pool
            const QString ASSIGNMENT_INSTANCES_HEADER = "ASSIGNMENT-INSTANCES";
            const QString ASSIGNMENT_POOL_HEADER = "ASSIGNMENT-POOL";
            
            QByteArray assignmentInstancesValue = connection->requestHeaders().value(ASSIGNMENT_INSTANCES_HEADER.toLocal8Bit());
            
            int numInstances = 1;
            
            if (!assignmentInstancesValue.isEmpty()) {
                // the user has requested a specific number of instances
                // so set that on the created assignment
                
                numInstances = assignmentInstancesValue.toInt();
            }
            
            QString assignmentPool = emptyPool;
            QByteArray assignmentPoolValue = connection->requestHeaders().value(ASSIGNMENT_POOL_HEADER.toLocal8Bit());
            
            if (!assignmentPoolValue.isEmpty()) {
                // specific pool requested, set that on the created assignment
                assignmentPool = QString(assignmentPoolValue);
            }
            
            
            for (int i = 0; i < numInstances; i++) {
                
                // create an assignment for this saved script
                Assignment* scriptAssignment = new Assignment(Assignment::CreateCommand, Assignment::AgentType, assignmentPool);
                
                QString newPath = pathForAssignmentScript(scriptAssignment->getUUID());
                
                // create a file with the GUID of the assignment in the script host location
                QFile scriptFile(newPath);
                scriptFile.open(QIODevice::WriteOnly);
                scriptFile.write(formData[0].second);
                
                qDebug() << qPrintable(QString("Saved a script for assignment at %1%2")
                                       .arg(newPath).arg(assignmentPool == emptyPool ? "" : " - pool is " + assignmentPool));
                
                // add the script assigment to the assignment queue
                SharedAssignmentPointer sharedScriptedAssignment(scriptAssignment);
                _unfulfilledAssignments.enqueue(sharedScriptedAssignment);
                _allAssignments.insert(sharedScriptedAssignment->getUUID(), sharedScriptedAssignment);
            }
            
            // respond with a 200 code for successful upload
            connection->respond(HTTPConnection::StatusCode200);
            
            return true;
        }
    } else if (connection->requestOperation() == QNetworkAccessManager::DeleteOperation) {
        const QString ALL_NODE_DELETE_REGEX_STRING = QString("\\%1\\/?$").arg(URI_NODES);
        const QString NODE_DELETE_REGEX_STRING = QString("\\%1\\/(%2)\\/$").arg(URI_NODES).arg(UUID_REGEX_STRING);
        
        QRegExp allNodesDeleteRegex(ALL_NODE_DELETE_REGEX_STRING);
        QRegExp nodeDeleteRegex(NODE_DELETE_REGEX_STRING);
       
        if (nodeDeleteRegex.indexIn(url.path()) != -1) {
            // this is a request to DELETE one node by UUID
            
            // pull the captured string, if it exists
            QUuid deleteUUID = QUuid(nodeDeleteRegex.cap(1));
            
            SharedNodePointer nodeToKill = LimitedNodeList::getInstance()->nodeWithUUID(deleteUUID);
            
            if (nodeToKill) {
                // start with a 200 response
                connection->respond(HTTPConnection::StatusCode200);
                
                // we have a valid UUID and node - kill the node that has this assignment
                QMetaObject::invokeMethod(LimitedNodeList::getInstance(), "killNodeWithUUID", Q_ARG(const QUuid&, deleteUUID));
                
                // successfully processed request
                return true;
            }
            
            return true;
        } else if (allNodesDeleteRegex.indexIn(url.path()) != -1) {
            qDebug() << "Received request to kill all nodes.";
            LimitedNodeList::getInstance()->eraseAllNodes();
            
            return true;
        }
    }
    
    // didn't process the request, let the HTTPManager try and handle
    return false;
}

bool DomainServer::handleHTTPSRequest(HTTPSConnection* connection, const QUrl &url) {
    const QString URI_OAUTH = "/oauth";
    if (url.path() == URI_OAUTH) {
        
        QUrlQuery codeURLQuery(url);
        
        const QString CODE_QUERY_KEY = "code";
        QString authorizationCode = codeURLQuery.queryItemValue(CODE_QUERY_KEY);
        
        const QString STATE_QUERY_KEY = "state";
        QUuid stateUUID = QUuid(codeURLQuery.queryItemValue(STATE_QUERY_KEY));
        
        
        if (!authorizationCode.isEmpty() && !stateUUID.isNull()) {
            // fire off a request with this code and state to get an access token for the user
            
            const QString OAUTH_TOKEN_REQUEST_PATH = "/oauth/token";
            QUrl tokenRequestUrl = _oauthProviderURL;
            tokenRequestUrl.setPath(OAUTH_TOKEN_REQUEST_PATH);
            
            const QString OAUTH_GRANT_TYPE_POST_STRING = "grant_type=authorization_code";
            QString tokenPostBody = OAUTH_GRANT_TYPE_POST_STRING;
            tokenPostBody += QString("&code=%1&redirect_uri=%2&client_id=%3&client_secret=%4")
                .arg(authorizationCode, oauthRedirectURL().toString(), _oauthClientID, _oauthClientSecret);
            
            QNetworkRequest tokenRequest(tokenRequestUrl);
            tokenRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
            
            QNetworkReply* tokenReply = _networkAccessManager->post(tokenRequest, tokenPostBody.toLocal8Bit());
            
            qDebug() << "Requesting a token for user with session UUID" << uuidStringWithoutCurlyBraces(stateUUID);
            
            // insert this to our pending token replies so we can associate the returned access token with the right UUID
            _networkReplyUUIDMap.insert(tokenReply, stateUUID);
            
            connect(tokenReply, &QNetworkReply::finished, this, &DomainServer::handleTokenRequestFinished);
        }
        
        // respond with a 200 code indicating that login is complete
        connection->respond(HTTPConnection::StatusCode200);
        
        return true;
    } else {
        return false;
    }
}

const QString OAUTH_JSON_ACCESS_TOKEN_KEY = "access_token";

void DomainServer::handleTokenRequestFinished() {
    QNetworkReply* networkReply = reinterpret_cast<QNetworkReply*>(sender());
    QUuid matchingSessionUUID = _networkReplyUUIDMap.take(networkReply);
    
    if (!matchingSessionUUID.isNull() && networkReply->error() == QNetworkReply::NoError) {
        // pull the access token from the returned JSON and store it with the matching session UUID
        QJsonDocument returnedJSON = QJsonDocument::fromJson(networkReply->readAll());
        QString accessToken = returnedJSON.object()[OAUTH_JSON_ACCESS_TOKEN_KEY].toString();
        
        qDebug() << "Received access token for user with UUID" << uuidStringWithoutCurlyBraces(matchingSessionUUID);
        
        // fire off a request to get this user's identity so we can see if we will let them in
        QUrl profileURL = _oauthProviderURL;
        profileURL.setPath("/api/v1/users/profile");
        profileURL.setQuery(QString("%1=%2").arg(OAUTH_JSON_ACCESS_TOKEN_KEY, accessToken));
        
        QNetworkReply* profileReply = _networkAccessManager->get(QNetworkRequest(profileURL));
        
        qDebug() << "Requesting access token for user with session UUID" << uuidStringWithoutCurlyBraces(matchingSessionUUID);
        
        connect(profileReply, &QNetworkReply::finished, this, &DomainServer::handleProfileRequestFinished);
        
        _networkReplyUUIDMap.insert(profileReply, matchingSessionUUID);
    }
}

void DomainServer::handleProfileRequestFinished() {
    QNetworkReply* networkReply = reinterpret_cast<QNetworkReply*>(sender());
    QUuid matchingSessionUUID = _networkReplyUUIDMap.take(networkReply);
    
    if (!matchingSessionUUID.isNull() && networkReply->error() == QNetworkReply::NoError) {
        QJsonDocument profileJSON = QJsonDocument::fromJson(networkReply->readAll());
        
        if (profileJSON.object()["status"].toString() == "success") {
            // pull the user roles from the response
            QJsonArray userRolesArray = profileJSON.object()["data"].toObject()["user"].toObject()["roles"].toArray();
            
            QJsonArray allowedRolesArray = _argumentVariantMap.value(ALLOWED_ROLES_CONFIG_KEY).toJsonValue().toArray();
            
            bool shouldAllowUserToConnect = false;
            
            foreach(const QJsonValue& roleValue, userRolesArray) {
                if (allowedRolesArray.contains(roleValue)) {
                    // the user has a role that lets them in
                    // set the bool to true and break
                    shouldAllowUserToConnect = true;
                    break;
                }
            }
            
            qDebug() << "Confirmed authentication state for user" << uuidStringWithoutCurlyBraces(matchingSessionUUID)
                << "-" << shouldAllowUserToConnect;
            
            // insert this UUID and a flag that indicates if they are allowed to connect
            _sessionAuthenticationHash.insert(matchingSessionUUID, shouldAllowUserToConnect);
        }
    }
}

void DomainServer::refreshStaticAssignmentAndAddToQueue(SharedAssignmentPointer& assignment) {
    QUuid oldUUID = assignment->getUUID();
    assignment->resetUUID();
    
    qDebug() << "Reset UUID for assignment -" << *assignment.data() << "- and added to queue. Old UUID was"
        << uuidStringWithoutCurlyBraces(oldUUID);
    
    if (assignment->getType() == Assignment::AgentType && assignment->getPayload().isEmpty()) {
        // if this was an Agent without a script URL, we need to rename the old file so it can be retrieved at the new UUID
        QFile::rename(pathForAssignmentScript(oldUUID), pathForAssignmentScript(assignment->getUUID()));
    }
    
    // add the static assignment back under the right UUID, and to the queue
    _allAssignments.insert(assignment->getUUID(), assignment);
    _unfulfilledAssignments.enqueue(assignment);
}

void DomainServer::nodeAdded(SharedNodePointer node) {
    // we don't use updateNodeWithData, so add the DomainServerNodeData to the node here
    node->setLinkedData(new DomainServerNodeData());
}

void DomainServer::nodeKilled(SharedNodePointer node) {
    
    DomainServerNodeData* nodeData = reinterpret_cast<DomainServerNodeData*>(node->getLinkedData());
    
    if (nodeData) {
        // if this node's UUID matches a static assignment we need to throw it back in the assignment queue
        if (!nodeData->getAssignmentUUID().isNull()) {
            SharedAssignmentPointer matchedAssignment = _allAssignments.take(nodeData->getAssignmentUUID());
            
            if (matchedAssignment && matchedAssignment->isStatic()) {
                refreshStaticAssignmentAndAddToQueue(matchedAssignment);
            }
        }
        
        // cleanup the connection secrets that we set up for this node (on the other nodes)
        foreach (const QUuid& otherNodeSessionUUID, nodeData->getSessionSecretHash().keys()) {
            SharedNodePointer otherNode = LimitedNodeList::getInstance()->nodeWithUUID(otherNodeSessionUUID);
            if (otherNode) {
                reinterpret_cast<DomainServerNodeData*>(otherNode->getLinkedData())->getSessionSecretHash().remove(node->getUUID());
            }
        }
    }
}

SharedAssignmentPointer DomainServer::matchingQueuedAssignmentForCheckIn(const QUuid& assignmentUUID, NodeType_t nodeType) {
    QQueue<SharedAssignmentPointer>::iterator i = _unfulfilledAssignments.begin();
    
    while (i != _unfulfilledAssignments.end()) {
        if (i->data()->getType() == Assignment::typeForNodeType(nodeType)
            && i->data()->getUUID() == assignmentUUID) {
            // we have an unfulfilled assignment to return
            
            // return the matching assignment
            return _unfulfilledAssignments.takeAt(i - _unfulfilledAssignments.begin());
        } else {
            ++i;
        }
    }
    
    return SharedAssignmentPointer();
}

SharedAssignmentPointer DomainServer::deployableAssignmentForRequest(const Assignment& requestAssignment) {
    // this is an unassigned client talking to us directly for an assignment
    // go through our queue and see if there are any assignments to give out
    QQueue<SharedAssignmentPointer>::iterator sharedAssignment = _unfulfilledAssignments.begin();

    while (sharedAssignment != _unfulfilledAssignments.end()) {
        Assignment* assignment = sharedAssignment->data();
        bool requestIsAllTypes = requestAssignment.getType() == Assignment::AllTypes;
        bool assignmentTypesMatch = assignment->getType() == requestAssignment.getType();
        bool nietherHasPool = assignment->getPool().isEmpty() && requestAssignment.getPool().isEmpty();
        bool assignmentPoolsMatch = assignment->getPool() == requestAssignment.getPool();
        
        if ((requestIsAllTypes || assignmentTypesMatch) && (nietherHasPool || assignmentPoolsMatch)) {
            
            // remove the assignment from the queue
            SharedAssignmentPointer deployableAssignment = _unfulfilledAssignments.takeAt(sharedAssignment
                                                                                          - _unfulfilledAssignments.begin());
            
            // until we get a connection for this assignment
            // put assignment back in queue but stick it at the back so the others have a chance to go out
            _unfulfilledAssignments.enqueue(deployableAssignment);
            
            // stop looping, we've handed out an assignment
            return deployableAssignment;
        } else {
            // push forward the iterator to check the next assignment
            ++sharedAssignment;
        }
    }
    
    return SharedAssignmentPointer();
}

void DomainServer::removeMatchingAssignmentFromQueue(const SharedAssignmentPointer& removableAssignment) {
    QQueue<SharedAssignmentPointer>::iterator potentialMatchingAssignment = _unfulfilledAssignments.begin();
    while (potentialMatchingAssignment != _unfulfilledAssignments.end()) {
        if (potentialMatchingAssignment->data()->getUUID() == removableAssignment->getUUID()) {
            _unfulfilledAssignments.erase(potentialMatchingAssignment);
            
            // we matched and removed an assignment, bail out
            break;
        } else {
            ++potentialMatchingAssignment;
        }
    }
}

void DomainServer::addStaticAssignmentsToQueue() {

    // if the domain-server has just restarted,
    // check if there are static assignments that we need to throw into the assignment queue
    QHash<QUuid, SharedAssignmentPointer> staticHashCopy = _allAssignments;
    QHash<QUuid, SharedAssignmentPointer>::iterator staticAssignment = staticHashCopy.begin();
    while (staticAssignment != staticHashCopy.end()) {
        // add any of the un-matched static assignments to the queue
        bool foundMatchingAssignment = false;
        
        // enumerate the nodes and check if there is one with an attached assignment with matching UUID
        foreach (const SharedNodePointer& node, LimitedNodeList::getInstance()->getNodeHash()) {
            if (node->getUUID() == staticAssignment->data()->getUUID()) {
                foundMatchingAssignment = true;
            }
        }
        
        if (!foundMatchingAssignment) {
            // this assignment has not been fulfilled - reset the UUID and add it to the assignment queue
            refreshStaticAssignmentAndAddToQueue(*staticAssignment);
        }
        
        ++staticAssignment;
    }
}
