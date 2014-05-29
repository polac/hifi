//
//  AccountManager.cpp
//  libraries/networking/src
//
//  Created by Stephen Birarda on 2/18/2014.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include <QtCore/QDataStream>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QStringList>
#include <QtCore/QUrlQuery>
#include <QtNetwork/QNetworkRequest>
#include <QHttpMultiPart>

#include "NodeList.h"
#include "PacketHeaders.h"

#include "AccountManager.h"

const bool VERBOSE_HTTP_REQUEST_DEBUGGING = false;

AccountManager& AccountManager::getInstance() {
    static AccountManager sharedInstance;
    return sharedInstance;
}

Q_DECLARE_METATYPE(OAuthAccessToken)
Q_DECLARE_METATYPE(DataServerAccountInfo)
Q_DECLARE_METATYPE(QNetworkAccessManager::Operation)
Q_DECLARE_METATYPE(JSONCallbackParameters)

const QString ACCOUNTS_GROUP = "accounts";

JSONCallbackParameters::JSONCallbackParameters() :
    jsonCallbackReceiver(NULL),
    jsonCallbackMethod(),
    errorCallbackReceiver(NULL),
    errorCallbackMethod(),
    updateReciever(NULL),
    updateSlot()
{
}

AccountManager::AccountManager() :
    _authURL(),
    _networkAccessManager(NULL),
    _pendingCallbackMap(),
    _accountInfo()
{
    qRegisterMetaType<OAuthAccessToken>("OAuthAccessToken");
    qRegisterMetaTypeStreamOperators<OAuthAccessToken>("OAuthAccessToken");
    
    qRegisterMetaType<DataServerAccountInfo>("DataServerAccountInfo");
    qRegisterMetaTypeStreamOperators<DataServerAccountInfo>("DataServerAccountInfo");
    
    qRegisterMetaType<QNetworkAccessManager::Operation>("QNetworkAccessManager::Operation");
    qRegisterMetaType<JSONCallbackParameters>("JSONCallbackParameters");
    
    connect(&_accountInfo, &DataServerAccountInfo::balanceChanged, this, &AccountManager::accountInfoBalanceChanged);
}

const QString DOUBLE_SLASH_SUBSTITUTE = "slashslash";

void AccountManager::logout() {
    // a logout means we want to delete the DataServerAccountInfo we currently have for this URL, in-memory and in file
    _accountInfo = DataServerAccountInfo();
    
    emit balanceChanged(0);
    connect(&_accountInfo, &DataServerAccountInfo::balanceChanged, this, &AccountManager::accountInfoBalanceChanged);
    
    QSettings settings;
    settings.beginGroup(ACCOUNTS_GROUP);
    
    QString keyURLString(_authURL.toString().replace("//", DOUBLE_SLASH_SUBSTITUTE));
    settings.remove(keyURLString);
    
    qDebug() << "Removed account info for" << _authURL << "from in-memory accounts and .ini file";
    
    emit logoutComplete();
    // the username has changed to blank
    emit usernameChanged(QString());
}

void AccountManager::updateBalance() {
    if (hasValidAccessToken()) {
        // ask our auth endpoint for our balance
        JSONCallbackParameters callbackParameters;
        callbackParameters.jsonCallbackReceiver = &_accountInfo;
        callbackParameters.jsonCallbackMethod = "setBalanceFromJSON";
        
        authenticatedRequest("/api/v1/wallets/mine", QNetworkAccessManager::GetOperation, callbackParameters);
    }
}

void AccountManager::accountInfoBalanceChanged(qint64 newBalance) {
    emit balanceChanged(newBalance);
}

void AccountManager::setAuthURL(const QUrl& authURL) {
    if (_authURL != authURL) {
        _authURL = authURL;
        
        qDebug() << "URL for node authentication has been changed to" << qPrintable(_authURL.toString());
        qDebug() << "Re-setting authentication flow.";
        
        // check if there are existing access tokens to load from settings
        QSettings settings;
        settings.beginGroup(ACCOUNTS_GROUP);
        
        foreach(const QString& key, settings.allKeys()) {
            // take a key copy to perform the double slash replacement
            QString keyCopy(key);
            QUrl keyURL(keyCopy.replace("slashslash", "//"));
            
            if (keyURL == _authURL) {
                // pull out the stored access token and store it in memory
                _accountInfo = settings.value(key).value<DataServerAccountInfo>();
                qDebug() << "Found a data-server access token for" << qPrintable(keyURL.toString());
                
                emit accessTokenChanged();
            }
        }
        
        // tell listeners that the auth endpoint has changed
        emit authEndpointChanged();
    }
}

void AccountManager::authenticatedRequest(const QString& path, QNetworkAccessManager::Operation operation,
                                          const JSONCallbackParameters& callbackParams,
                                          const QByteArray& dataByteArray,
                                          QHttpMultiPart* dataMultiPart) {
    QMetaObject::invokeMethod(this, "invokedRequest",
                              Q_ARG(const QString&, path),
                              Q_ARG(QNetworkAccessManager::Operation, operation),
                              Q_ARG(const JSONCallbackParameters&, callbackParams),
                              Q_ARG(const QByteArray&, dataByteArray),
                              Q_ARG(QHttpMultiPart*, dataMultiPart));
}

void AccountManager::invokedRequest(const QString& path, QNetworkAccessManager::Operation operation,
                                    const JSONCallbackParameters& callbackParams,
                                    const QByteArray& dataByteArray, QHttpMultiPart* dataMultiPart) {
    
    if (!_networkAccessManager) {
        _networkAccessManager = new QNetworkAccessManager(this);
    }
    
    if (hasValidAccessToken()) {
        QNetworkRequest authenticatedRequest;
        
        QUrl requestURL = _authURL;
        
        if (path.startsWith("/")) {
            requestURL.setPath(path);
        } else {
            requestURL.setPath("/" + path);
        }
        
        requestURL.setQuery("access_token=" + _accountInfo.getAccessToken().token);
        
        authenticatedRequest.setUrl(requestURL);
        
        if (VERBOSE_HTTP_REQUEST_DEBUGGING) {
            qDebug() << "Making an authenticated request to" << qPrintable(requestURL.toString());
            
            if (!dataByteArray.isEmpty()) {
                qDebug() << "The POST/PUT body -" << QString(dataByteArray);
            }
        }
        
        QNetworkReply* networkReply = NULL;
        
        switch (operation) {
            case QNetworkAccessManager::GetOperation:
                networkReply = _networkAccessManager->get(authenticatedRequest);
                break;
            case QNetworkAccessManager::PostOperation:
            case QNetworkAccessManager::PutOperation:
                if (dataMultiPart) {
                    if (operation == QNetworkAccessManager::PostOperation) {
                        networkReply = _networkAccessManager->post(authenticatedRequest, dataMultiPart);
                    } else {
                        networkReply = _networkAccessManager->put(authenticatedRequest, dataMultiPart);
                    }
                    dataMultiPart->setParent(networkReply);
                } else {
                    authenticatedRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
                    if (operation == QNetworkAccessManager::PostOperation) {
                        networkReply = _networkAccessManager->post(authenticatedRequest, dataByteArray);
                    } else {
                        networkReply = _networkAccessManager->put(authenticatedRequest, dataByteArray);
                    }
                }
                
                break;
            default:
                // other methods not yet handled
                break;
        }
        
        if (networkReply) {
            if (!callbackParams.isEmpty()) {
                // if we have information for a callback, insert the callbackParams into our local map
                _pendingCallbackMap.insert(networkReply, callbackParams);
                
                if (callbackParams.updateReciever && !callbackParams.updateSlot.isEmpty()) {
                    callbackParams.updateReciever->connect(networkReply, SIGNAL(uploadProgress(qint64, qint64)),
                                                            callbackParams.updateSlot.toStdString().c_str());
                }
            }
            
            // if we ended up firing of a request, hook up to it now
            connect(networkReply, SIGNAL(finished()), SLOT(processReply()));
        }
    }
}

void AccountManager::processReply() {
    QNetworkReply* requestReply = reinterpret_cast<QNetworkReply*>(sender());
    
    if (requestReply->error() == QNetworkReply::NoError) {
        passSuccessToCallback(requestReply);
    } else {
        passErrorToCallback(requestReply);
    }
    delete requestReply;
}

void AccountManager::passSuccessToCallback(QNetworkReply* requestReply) {
    QJsonDocument jsonResponse = QJsonDocument::fromJson(requestReply->readAll());
    
    JSONCallbackParameters callbackParams = _pendingCallbackMap.value(requestReply);
    
    if (callbackParams.jsonCallbackReceiver) {
        // invoke the right method on the callback receiver
        QMetaObject::invokeMethod(callbackParams.jsonCallbackReceiver, qPrintable(callbackParams.jsonCallbackMethod),
                                  Q_ARG(const QJsonObject&, jsonResponse.object()));
        
        // remove the related reply-callback group from the map
        _pendingCallbackMap.remove(requestReply);
        
    } else {
        if (VERBOSE_HTTP_REQUEST_DEBUGGING) {
            qDebug() << "Received JSON response from data-server that has no matching callback.";
            qDebug() << jsonResponse;
        }
    }
}

void AccountManager::passErrorToCallback(QNetworkReply* requestReply) {
    JSONCallbackParameters callbackParams = _pendingCallbackMap.value(requestReply);
    
    if (callbackParams.errorCallbackReceiver) {
        // invoke the right method on the callback receiver
        QMetaObject::invokeMethod(callbackParams.errorCallbackReceiver, qPrintable(callbackParams.errorCallbackMethod),
                                  Q_ARG(QNetworkReply::NetworkError, requestReply->error()),
                                  Q_ARG(const QString&, requestReply->errorString()));
        
        // remove the related reply-callback group from the map
        _pendingCallbackMap.remove(requestReply);
    } else {
        if (VERBOSE_HTTP_REQUEST_DEBUGGING) {
            qDebug() << "Received error response from data-server that has no matching callback.";
            qDebug() << "Error" << requestReply->error() << "-" << requestReply->errorString();
        }
    }
}

bool AccountManager::hasValidAccessToken() {
    
    if (_accountInfo.getAccessToken().token.isEmpty() || _accountInfo.getAccessToken().isExpired()) {
        if (VERBOSE_HTTP_REQUEST_DEBUGGING) {
            qDebug() << "An access token is required for requests to" << qPrintable(_authURL.toString());
        }
        
        return false;
    } else {
        return true;
    }
}

bool AccountManager::checkAndSignalForAccessToken() {
    bool hasToken = hasValidAccessToken();
    
    if (!hasToken) {
        // emit a signal so somebody can call back to us and request an access token given a username and password
        emit authRequired();
    }
    
    return hasToken;
}

void AccountManager::requestAccessToken(const QString& login, const QString& password) {

    if (!_networkAccessManager) {
        _networkAccessManager = new QNetworkAccessManager(this);
    }

    QNetworkRequest request;
    
    QUrl grantURL = _authURL;
    grantURL.setPath("/oauth/token");
    
    const QString ACCOUNT_MANAGER_REQUESTED_SCOPE = "owner";
    
    QByteArray postData;
    postData.append("grant_type=password&");
    postData.append("username=" + login + "&");
    postData.append("password=" + password + "&");
    postData.append("scope=" + ACCOUNT_MANAGER_REQUESTED_SCOPE);
    
    request.setUrl(grantURL);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    
    QNetworkReply* requestReply = _networkAccessManager->post(request, postData);
    connect(requestReply, &QNetworkReply::finished, this, &AccountManager::requestFinished);
    connect(requestReply, SIGNAL(error(QNetworkReply::NetworkError)), this, SLOT(requestError(QNetworkReply::NetworkError)));
}


void AccountManager::requestFinished() {
    QNetworkReply* requestReply = reinterpret_cast<QNetworkReply*>(sender());
    
    QJsonDocument jsonResponse = QJsonDocument::fromJson(requestReply->readAll());
    const QJsonObject& rootObject = jsonResponse.object();
    
    if (!rootObject.contains("error")) {
        // construct an OAuthAccessToken from the json object
        
        if (!rootObject.contains("access_token") || !rootObject.contains("expires_in")
            || !rootObject.contains("token_type")) {
            // TODO: error handling - malformed token response
            qDebug() << "Received a response for password grant that is missing one or more expected values.";
        } else {
            // clear the path from the response URL so we have the right root URL for this access token
            QUrl rootURL = requestReply->url();
            rootURL.setPath("");
            
            qDebug() << "Storing an account with access-token for" << qPrintable(rootURL.toString());
            
            _accountInfo = DataServerAccountInfo(rootObject);
            
            emit loginComplete(rootURL);
            // the username has changed to whatever came back
            emit usernameChanged(_accountInfo.getUsername());
            
            // we have found or requested an access token
            emit accessTokenChanged();
            
            // store this access token into the local settings
            QSettings localSettings;
            localSettings.beginGroup(ACCOUNTS_GROUP);
            localSettings.setValue(rootURL.toString().replace("//", DOUBLE_SLASH_SUBSTITUTE),
                                   QVariant::fromValue(_accountInfo));
        }
    } else {
        // TODO: error handling
        qDebug() <<  "Error in response for password grant -" << rootObject["error_description"].toString();
        emit loginFailed();
    }
}

void AccountManager::requestError(QNetworkReply::NetworkError error) {
    // TODO: error handling
    qDebug() << "AccountManager requestError - " << error;
}
