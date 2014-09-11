/*
 * This file was generated by qdbusxml2cpp version 0.8
 * Command line was: qdbusxml2cpp -v org.neard.Manager.xml -p manager_p -v
 *
 * qdbusxml2cpp is Copyright (C) 2014 Digia Plc and/or its subsidiary(-ies).
 *
 * This is an auto-generated file.
 * Do not edit! All changes made to it will be lost.
 */

#ifndef MANAGER_P_H_1410442485
#define MANAGER_P_H_1410442485

#include <QtCore/QObject>
#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMap>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtDBus/QtDBus>

/*
 * Proxy class for interface org.neard.Manager
 */
class OrgNeardManagerInterface: public QDBusAbstractInterface
{
    Q_OBJECT
public:
    static inline const char *staticInterfaceName()
    { return "org.neard.Manager"; }

public:
    OrgNeardManagerInterface(const QString &service, const QString &path, const QDBusConnection &connection, QObject *parent = 0);

    ~OrgNeardManagerInterface();

public Q_SLOTS: // METHODS
    inline QDBusPendingReply<QVariantMap> GetProperties()
    {
        QList<QVariant> argumentList;
        return asyncCallWithArgumentList(QStringLiteral("GetProperties"), argumentList);
    }

    inline QDBusPendingReply<> RegisterHandoverAgent(const QDBusObjectPath &path)
    {
        QList<QVariant> argumentList;
        argumentList << QVariant::fromValue(path);
        return asyncCallWithArgumentList(QStringLiteral("RegisterHandoverAgent"), argumentList);
    }

    inline QDBusPendingReply<> RegisterNDEFAgent(const QDBusObjectPath &path, const QString &type)
    {
        QList<QVariant> argumentList;
        argumentList << QVariant::fromValue(path) << QVariant::fromValue(type);
        return asyncCallWithArgumentList(QStringLiteral("RegisterNDEFAgent"), argumentList);
    }

    inline QDBusPendingReply<> SetProperty(const QString &name, const QDBusVariant &value)
    {
        QList<QVariant> argumentList;
        argumentList << QVariant::fromValue(name) << QVariant::fromValue(value);
        return asyncCallWithArgumentList(QStringLiteral("SetProperty"), argumentList);
    }

    inline QDBusPendingReply<> UnregisterHandoverAgent(const QDBusObjectPath &path)
    {
        QList<QVariant> argumentList;
        argumentList << QVariant::fromValue(path);
        return asyncCallWithArgumentList(QStringLiteral("UnregisterHandoverAgent"), argumentList);
    }

    inline QDBusPendingReply<> UnregisterNDEFAgent(const QDBusObjectPath &path, const QString &type)
    {
        QList<QVariant> argumentList;
        argumentList << QVariant::fromValue(path) << QVariant::fromValue(type);
        return asyncCallWithArgumentList(QStringLiteral("UnregisterNDEFAgent"), argumentList);
    }

Q_SIGNALS: // SIGNALS
    void AdapterAdded(const QDBusObjectPath &adapter);
    void AdapterRemoved(const QDBusObjectPath &adapter);
    void PropertyChanged(const QString &name, const QDBusVariant &value);
};

namespace org {
  namespace neard {
    typedef ::OrgNeardManagerInterface Manager;
  }
}
#endif
