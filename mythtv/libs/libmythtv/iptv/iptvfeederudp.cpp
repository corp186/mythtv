/** -*- Mode: c++ -*-
 *  IPTVFeederUdp
 *  Copyright (c) 2006 by Mike Mironov & Mickaël Remars
 *  Distributed as part of MythTV under GPL v2 and later.
 */
#define _INC_ERRNO

#include <algorithm>

#include "iptvfeederudp.h"

// Qt headers
#include <QUrl>

// Live555 headers
#include <BasicUsageEnvironment.hh>
#include <Groupsock.hh>
#include <GroupsockHelper.hh>
#include <BasicUDPSource.hh>
#include <TunnelEncaps.hh>

// MythTV headers
#include "iptvmediasink.h"
#include "mythcontext.h"
#include "mythlogging.h"
#include "tspacket.h"

#define LOC QString("IPTVFeedUDP: ")

IPTVFeederUDP::IPTVFeederUDP() :
    _source(NULL),
    _sink(NULL)
{
    LOG(VB_RECORD, LOG_INFO, LOC + "ctor -- success");
}

IPTVFeederUDP::~IPTVFeederUDP()
{
    LOG(VB_RECORD, LOG_INFO, LOC + "dtor -- begin");
    Close();
    LOG(VB_RECORD, LOG_INFO, LOC + "dtor -- end");
}

bool IPTVFeederUDP::IsUDP(const QString &url)
{
    return url.startsWith("udp://", Qt::CaseInsensitive);
}

bool IPTVFeederUDP::Open(const QString &url)
{
    LOG(VB_RECORD, LOG_INFO, LOC + QString("Open(%1) -- begin").arg(url));

    QMutexLocker locker(&_lock);

    if (_source)
    {
        LOG(VB_RECORD, LOG_INFO, LOC + "Open() -- end 1");
        return true;
    }

    QUrl parse(url);
    if (!parse.isValid() || parse.host().isEmpty() || (-1 == parse.port()))
    {
        LOG(VB_RECORD, LOG_INFO, LOC + "Open() -- end 2");
        return false;
    }

    struct in_addr addr;
    QByteArray host = parse.host().toLatin1();
    addr.s_addr = our_inet_addr(host.constData());

    // Begin by setting up our usage environment:
    if (!InitEnv())
        return false;

    ReceivingSocketAddr = our_inet_addr(parse.host().toLatin1());

    Groupsock *socket = new Groupsock(*_live_env, addr, parse.port(), 0);
    if (!socket)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to create Live UDP Socket.");
        FreeEnv();
        return false;
    }
    _source = BasicUDPSource::createNew(*_live_env, socket);
    if (!_source)
    {
        LOG(VB_GENERAL, LOG_ERR, LOC + "Failed to create Live UDP Source.");

        if (socket)
            delete socket;

        FreeEnv();
        return false;
    }

    _sink = IPTVMediaSink::CreateNew(*_live_env, TSPacket::kSize * 128*1024);
    if (!_sink)
    {
        LOG(VB_GENERAL, LOG_ERR, QString("IPTV # Failed to create sink: %1")
                .arg(_live_env->getResultMsg()));

        Medium::close(_source);
        _source = NULL;
        if (socket)
            delete socket;
        FreeEnv();

        return false;
    }

    _sink->startPlaying(*_source, NULL, NULL);
    vector<TSDataListener*>::iterator it = _listeners.begin();
    for (; it != _listeners.end(); ++it)
        _sink->AddListener(*it);

    LOG(VB_RECORD, LOG_INFO, LOC + "Open() -- end");

    return true;
}

void IPTVFeederUDP::Close(void)
{
    LOG(VB_RECORD, LOG_INFO, LOC + "Close() -- begin");
    Stop();

    QMutexLocker locker(&_lock);

    if (_sink)
    {
        Medium::close(_sink);
        _sink = NULL;
    }

    if (_source)
    {
        Groupsock *socket = _source->gs();
        Medium::close(_source);
        _source = NULL;
        if (socket)
            delete socket;
    }

    FreeEnv();

    LOG(VB_RECORD, LOG_INFO, LOC + "Close() -- end");
}

void IPTVFeederUDP::AddListener(TSDataListener *item)
{
    LOG(VB_RECORD, LOG_INFO, LOC + QString("AddListener(0x%1) -- begin")
                       .arg((uint64_t)item,0,16));
    if (!item)
    {
        LOG(VB_RECORD, LOG_INFO, LOC + QString("AddListener(0x%1) -- end")
                           .arg((uint64_t)item,0,16));
        return;
    }

    // avoid duplicates
    RemoveListener(item);

    // add to local list
    QMutexLocker locker(&_lock);
    _listeners.push_back(item);

    if (_sink)
        _sink->AddListener(item);

    LOG(VB_RECORD, LOG_INFO, LOC + QString("AddListener(0x%1) -- end")
                       .arg((uint64_t)item,0,16));
}

void IPTVFeederUDP::RemoveListener(TSDataListener *item)
{
    LOG(VB_RECORD, LOG_INFO, LOC + QString("RemoveListener(0x%1) -- begin")
                       .arg((uint64_t)item,0,16));
    QMutexLocker locker(&_lock);
    vector<TSDataListener*>::iterator it =
        find(_listeners.begin(), _listeners.end(), item);

    if (it == _listeners.end())
    {
        LOG(VB_RECORD, LOG_INFO, LOC + QString("RemoveListener(0x%1) -- end 1")
                           .arg((uint64_t)item,0,16));
        return;
    }

    // remove from local list..
    *it = *_listeners.rbegin();
    _listeners.resize(_listeners.size() - 1);

    if (_sink)
        _sink->RemoveListener(item);

    LOG(VB_RECORD, LOG_INFO, LOC + QString("RemoveListener(0x%1) -- end 2")
                       .arg((uint64_t)item,0,16));
}
