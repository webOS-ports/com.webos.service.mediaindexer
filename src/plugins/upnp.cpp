// Copyright (c) 2019-2020 LG Electronics, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// SPDX-License-Identifier: Apache-2.0

#include "upnp.h"

#include <string>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <thread>

#include "upnptools.h"

std::shared_ptr<Plugin> Upnp::instance_ = nullptr;

const char *Upnp::upnpDeviceCategory_ = "urn:schemas-upnp-org:device:MediaServer:1";
const char *Upnp::upnpServiceCategory_ = "urn:schemas-upnp-org:service:ContentDirectory:1";
const int Upnp::upnpSearchTimeout_ = 5; // in [s]

std::string Upnp::uri("upnp");
std::shared_ptr<Plugin> Upnp::instance()
{
    if (!Upnp::instance_)
        Upnp::instance_ = std::make_shared<Upnp>();
    return Upnp::instance_;
}

Upnp::Upnp() : Plugin(Upnp::uri)
{
    upnpHandle_ = -1;
    auto err = UpnpInit2(NULL, 0);
    if (err)
        LOG_CRITICAL(MEDIA_INDEXER_UPNP, 0, "UpnpInit2() failed (%i)", err);
}

Upnp::~Upnp()
{
    // Unwind UPnP stack.
    runDeviceDetection(false);
    auto err = UpnpFinish();
    if (err)
        LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "UpnpFinish() failed (%i)", err);
}

UpnpClient_Handle Upnp::upnpHandle(void) const
{
    return upnpHandle_;
}

void Upnp::scan(const std::string &uri)
{
    // first get the device from the uri
    auto dev = device(uri);
    if (!dev)
        return;

    LOG_INFO(MEDIA_INDEXER_UPNP, 0, "Start item-tree-walk on device '%s'", dev->uri().c_str());

    // all browsing happens synchronously so this thread will stay
    // alive until either browsing is done or an error occurs
    sendBrowseRequest("0", 0, dev);

    LOG_INFO(MEDIA_INDEXER_UPNP, 0, "Item-tree-walk on device '%s' has been completed",
        dev->uri().c_str());
}

void Upnp::extractMeta(MediaItem &mediaItem, bool expand)
{
    auto path = mediaItem.path();
    LOG_INFO(MEDIA_INDEXER_UPNP, 0, "Request meta data for item '%s'", path.c_str());
    sendMetaRequest(path, mediaItem);
}

std::optional<std::string> Upnp::getPlaybackUri(const std::string &uri)
{
    auto dev = device(uri);
    if (!dev)
        return std::nullopt;

    if (!dev->available())
        return std::nullopt;

    // get the upnp object id from the media item uri
    auto id = uri;
    // remove the device uri and the '/' separator from the path
    id.erase(0, dev->uri().length() + 1);

    IXML_Document *didl = getObjectMeta(id, dev);
    if (!didl)
        return std::nullopt;

    std::string pbUri = getNodeText(didl, "res");
    if (!pbUri.length())
        return std::nullopt;

    ixmlDocument_free(didl);

    LOG_DEBUG(MEDIA_INDEXER_UPNP, "Playback uri for '%s' is '%s'", uri.c_str(), pbUri.c_str());
    return pbUri;
}

int Upnp::eventCallback(Upnp_EventType eventType, const void *event,
    void *cookie)
{
    switch (eventType) {
    case UPNP_CONTROL_ACTION_REQUEST:
        [[fallthrough]]
    case UPNP_CONTROL_GET_VAR_REQUEST:
        [[fallthrough]]
    case UPNP_CONTROL_ACTION_COMPLETE:
        [[fallthrough]]
    case UPNP_CONTROL_GET_VAR_COMPLETE:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP control event, ignore");
        break;
    case UPNP_DISCOVERY_ADVERTISEMENT_ALIVE:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP advertisement alive event");
        static_cast<Upnp *>(cookie)->serviceFound(event);
        break;
    case UPNP_DISCOVERY_ADVERTISEMENT_BYEBYE:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP advertisement byebye event");
        static_cast<Upnp *>(cookie)->serviceLost(event);
        break;
    case UPNP_DISCOVERY_SEARCH_RESULT:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP advertisement search result event");
        /// @todo To satisfy the hack for advertisement events we send
        /// void**.
        static_cast<Upnp *>(cookie)->serviceFound(&event);
        break;
    case UPNP_DISCOVERY_SEARCH_TIMEOUT: {
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP advertisement search timeout, respawn");
        auto plugin = static_cast<Upnp *>(cookie);
        plugin->checkDevices();
        UpnpSearchAsync(plugin->upnpHandle(), upnpSearchTimeout_,
            upnpDeviceCategory_, cookie);
        break;
    }
    case UPNP_EVENT_SUBSCRIPTION_REQUEST:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP subscription request, ignore");
        break;
    case UPNP_EVENT_RECEIVED:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP received received, ignore");
        break;
    case UPNP_EVENT_RENEWAL_COMPLETE:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP renewal complete");
        break;
    case UPNP_EVENT_SUBSCRIBE_COMPLETE:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP subscribe complete");
        break;
    case UPNP_EVENT_UNSUBSCRIBE_COMPLETE:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP unsubscribe complete");
        break;
    case UPNP_EVENT_AUTORENEWAL_FAILED:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP auto-renewal complete");
        break;
    case UPNP_EVENT_SUBSCRIPTION_EXPIRED:
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "UPnP subscription expired");
        break;
    default:  // unknown upnp event
        LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "Unknown UPnP event: %i", eventType);
    }

    return 0;
}

IXML_Document *Upnp::actionResult(UpnpActionComplete *event)
{
    auto *doc = UpnpActionComplete_get_ActionResult(event);
    if (!doc)
        return nullptr;

    auto tag = ixmlDocument_getElementById(doc, "Result");
    auto str = ixmlNode_getFirstChild(&tag->n);
    if (ixmlNode_getNodeType(str) != eTEXT_NODE)
        return nullptr;

    auto res = ixmlParseBuffer(ixmlNode_getNodeValue(str));

    return res;
}

void Upnp::getDeviceMeta(Upnp *plugin, std::string uri, std::string location)
{
    // now read the service description xml
    IXML_Document *descDoc = nullptr;
    std::string meta;
    const DOMString t;

    LOG_DEBUG(MEDIA_INDEXER_UPNP, "Get meta data from: '%s'", location.c_str());

    UpnpDownloadXmlDoc(location.c_str(), &descDoc);
    if (!descDoc)
        return;

    // strip the base uri from the location
    auto baseUri(location);
    auto idx = baseUri.begin() + 8; // 'http://' or 'https://'
    // search for first '/' after protocol identifer
    idx = std::find(idx, baseUri.end(), '/');
    baseUri.erase(idx, baseUri.end());

    // blacklist the plugin if the required service is not found
    auto controlUrl = plugin->checkServiceCategory(descDoc);
    auto mp = baseUri;
    if (!controlUrl) {
        std::lock_guard<std::mutex> lock(plugin->blacklistLock_);
        plugin->blacklist_.push_back(uri);
        goto freeDoc;
    }

    mp += controlUrl;

    // now that we now the device supports the service we are looking
    // for let's announce it to the observers
    plugin->addDevice(uri, mp, "", 4);

    t = plugin->getNodeText(descDoc, "friendlyName");
    if (!!t) {
        meta = t;
        plugin->modifyDevice(uri, Device::Meta::Name, meta);
    }

    t = plugin->getNodeText(descDoc, "modelDescription");
    if (!!t) {
        meta = t;
        plugin->modifyDevice(uri, Device::Meta::Description, meta);
    }

    t = plugin->getNodeText(descDoc, "url"); // is the url inside <icon>, we
    // take the first icon
    if (!!t) {
        meta = baseUri + t;
        plugin->modifyDevice(uri, Device::Meta::Icon, meta);
    }

    // cleanup
 freeDoc:
    ixmlDocument_free(descDoc);
}

int Upnp::runDeviceDetection(bool start)
{
    if (start) {
        auto err = UpnpRegisterClient(Upnp::eventCallback,
            static_cast<void *>(this), &upnpHandle_);
        if (err)
            LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "UpnpRegisterClient() failed (%i)", err);

        UpnpSearchAsync(upnpHandle_, upnpSearchTimeout_,
            upnpDeviceCategory_, static_cast<void *>(this));
    } else {
        auto err = UpnpUnRegisterClient(upnpHandle_);
        if (err)
            LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "UpnpUnRegisterClient() failed (%i)", err);
    }
    return 0;
}

void Upnp::serviceFound(const void *event)
{
    /// @todo Remove ugly event hack once libupnp includes 6556b0b.
    UpnpDiscovery *discovery = (UpnpDiscovery *) *((void **) event);
    const UpnpString *deviceId = UpnpDiscovery_get_DeviceID(discovery);
    const UpnpString *location = UpnpDiscovery_get_Location(discovery);
    std::string uri = mangleUri(deviceId);

    LOG_DEBUG(MEDIA_INDEXER_UPNP, "Device found, constructed uri: '%s'", uri.c_str());

    {
        // check if device is blacklisted and shall be ignored
        std::lock_guard<std::mutex> lock(blacklistLock_);
        auto it = std::find(blacklist_.begin(), blacklist_.end(), uri);
        if (it != blacklist_.end()) {
            LOG_DEBUG(MEDIA_INDEXER_UPNP, "Device '%s' blacklisted, ignore", uri.c_str());
            return;
        }
    }

    // check if device is already known to plugin, for upnp devices
    // the device is created only if the meta data is fully available
    // as the service check requires the successful description
    // download so it is save to not get new meta data if the device
    // is already known
    auto dev = device(uri);
    bool requestMeta = !dev;
    if (!!dev) {
        requestMeta = !dev->available();
        addDevice(uri, 4); // refresh the available state as this
                           // device works with timeouts
    }

    if (requestMeta) {
        // now request the meta data for the new device, make a copy of
        // location which is destroyed when this method returns
        std::string loc(UpnpString_get_String(location));
        std::thread t(getDeviceMeta, this, uri, loc);
        t.detach(); // worker thread
    }
}

void Upnp::serviceLost(const void *event)
{
    /// @todo Remove ugly event hack once libupnp includes 6556b0b.
    UpnpDiscovery *discovery = (UpnpDiscovery *) *((void **) event);
    const UpnpString *deviceId = UpnpDiscovery_get_DeviceID(discovery);
    std::string uri = mangleUri(deviceId);
    LOG_DEBUG(MEDIA_INDEXER_UPNP, "Device said byebye, constructed uri: '%s'", uri.c_str());
    removeDevice(uri);
}

std::string Upnp::mangleUri(const UpnpString *deviceId)
{
    std::string uri(UpnpString_get_String(deviceId));

    // remove leading 'uuid:'
    if (!uri.compare(0, 5, "uuid:"))
        uri.erase(0, 5);

    // replace colons and whitespace
    std::replace_if(uri.begin(), uri.end(),
        [](char c) { return (c == ':'); }, '-');
    std::replace_if(uri.begin(), uri.end(),
        [](char c) { return std::isspace(c); }, '-');

    // make it a uri
    uri.insert(0, Upnp::uri + "://");

    return uri;
}

IXML_Node *Upnp::getChildNode(IXML_Node *node, const char *tagName) const
{
    auto children = ixmlNode_getChildNodes(node);
    auto child = children;
    IXML_Node *ret = nullptr;

    while (child) {
        if (!strcmp(ixmlNode_getNodeName(child->nodeItem), tagName)) {
            ret = child->nodeItem;
            break;
        }
        child = child->next;
    }

    ixmlNodeList_free(children);
    return ret;
}

const DOMString Upnp::getNodeText(IXML_Document *doc, const char *tagName) const
{
    // get the xml element
    auto elem =
        ixmlDocument_getElementById(doc, tagName);
    if (!elem)
        return nullptr;

    return getNodeText(&elem->n);
}

const DOMString Upnp::getNodeText(IXML_Node *node, const char *tagName) const
{
    auto n = getChildNode(node, tagName);
    if (!n)
        return nullptr;

    const auto text = getNodeText(n);
    return text;
}

const DOMString Upnp::getNodeText(IXML_Node *node) const
{
    // get the child element which is supposed to be a text element
    auto child = ixmlNode_getFirstChild(node);
    if (!child)
        return nullptr;
    if (ixmlNode_getNodeType(child) != eTEXT_NODE)
        return nullptr;

    // return the node text
    return ixmlNode_getNodeValue(child);
}

DOMString Upnp::getAttributeText(IXML_Node *node, const char *attrName) const
{
    DOMString ret = nullptr;

    auto attrMap = ixmlNode_getAttributes(node);
    auto attr = ixmlNamedNodeMap_getNamedItem(attrMap, attrName);
    if (!attr)
        goto out;

    ret = strdup(ixmlNode_getNodeValue(attr));

 out:
    ixmlNamedNodeMap_free(attrMap);
    return ret;
}

void Upnp::iterateOnTag(IXML_Document *doc, const char *tagName,
    const std::function<void(IXML_Node*)> &func) const
{
    auto tags = ixmlDocument_getElementsByTagName(doc, tagName);
    auto tag = tags;
    while (tag) {
        func(tag->nodeItem);
        tag = tag->next;
    }
    ixmlNodeList_free(tags);
}

const char *Upnp::checkServiceCategory(IXML_Document *doc) const
{
    const char *controlUrl = nullptr;

    // define lambda for the iterator
    auto check = [this, &controlUrl](IXML_Node *node) {
        auto sType = getNodeText(node, "serviceType");
        auto ctrlUrl = getNodeText(node, "controlURL");
        if (!strcmp(sType, Upnp::upnpServiceCategory_))
            controlUrl = ctrlUrl;
    };
    iterateOnTag(doc, "service", check);

    return controlUrl;
}

bool Upnp::sendMetaRequest(const std::string &id, MediaItem &mediaItem) const
{
    IXML_Document *didl = getObjectMeta(id, mediaItem.device());
    if (!didl)
        return false;

    parseMetaDataResponse(didl, mediaItem);
    ixmlDocument_free(didl);

    return true;
}

bool Upnp::sendBrowseRequest(const std::string &id, int count,
    std::shared_ptr<Device> device) const
{
    int done = 0;

    if (!count) {
        browseChunk(id, 0, 0, device);
    } else {
        while (done < count) {
            auto c = browseChunk(id, done, 10, device);
            if (c < 0) {
                LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "Browse failed");
                break;
            }
            done += c;
            LOG_DEBUG(MEDIA_INDEXER_UPNP, "Got %i, have %i, expecting %i", c, done, count);
        }
    }

    return (done == count);
}

int Upnp::browseChunk(const std::string &id, int start, int count,
    std::shared_ptr<Device> device) const
{
    IXML_Document *action;
    action = UpnpMakeAction("Browse", Upnp::upnpServiceCategory_, 6,
        "ObjectID", id.c_str(),
        "BrowseFlag", "BrowseDirectChildren",
        "Filter", "*",
        "StartingIndex", std::to_string(start).c_str(),
        "RequestedCount", std::to_string(count).c_str(),
        "SortCriteria", "");

    if (!action)
        return -1;

    IXML_Document *resp = nullptr;
    {
        std::lock_guard<std::mutex> lock(browseLock_);

        auto err = UpnpSendAction(upnpHandle_, device->mountpoint().c_str(),
            Upnp::upnpServiceCategory_, NULL, action, &resp);

        // we don't need this anymore
        ixmlDocument_free(action);

        if (err != UPNP_E_SUCCESS || !resp) {
            LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "Failed to send action to '%s': %i",
                device->uri().c_str(), err);
            if (resp)
                ixmlDocument_free(resp);
            return -1;
        }
    }

    // how many matches did we receive
    auto num = std::stoi(getNodeText(resp, "NumberReturned"));

    // if there are no matches skip this
    auto total = std::stoi(getNodeText(resp, "TotalMatches"));
    if (!total) {
        ixmlDocument_free(resp);
        return -1;
    }

    // now get the DIDL text
    auto didl = getNodeText(resp, "Result");
    if (!didl) {
        LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "No browse DIDL result");
        ixmlDocument_free(resp);
        return -1;
    }
    // parse it into its own document
    ixmlRelaxParser(1);
    IXML_Document *didlDoc = ixmlParseBuffer(didl);
    if (!didlDoc) {
        LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "DIDL parsing failed: %s", didl);
        ixmlDocument_free(resp);
        return -1;
    }

    // and throw it into the parser
    auto res = parseBrowseResponse(didlDoc, device);

    ixmlDocument_free(didlDoc);
    ixmlDocument_free(resp);

    return (res ? num : -1);
}

bool Upnp::parseBrowseResponse(IXML_Document *doc,
    std::shared_ptr<Device> device) const
{
    // send browse requests on containers
    auto diveIn = [this, device](IXML_Node *node) {
        // no children - ignore
        auto cs = getAttributeText(node, "childCount");
        auto c = std::stoi(cs);
        free(cs);
        if (!c)
            return;
        // check if we want to follow this container type
        auto upnpClass = getNodeText(node, "upnp:class");
        if (!upnpClassCheck(upnpClass)) {
            LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "Ignore UPNP class %s", upnpClass);
            return;
        }
        LOG_WARNING(MEDIA_INDEXER_UPNP, 0, "Dive into UPNP class %s", upnpClass);
        auto id = getAttributeText(node, "id");
        LOG_DEBUG(MEDIA_INDEXER_UPNP, "Browse %i from %s", c, id);
        sendBrowseRequest(id, c, device);
        free(id);
    };
    iterateOnTag(doc, "container", diveIn);

    // create media items for items
    auto handleItems = [this, &device](IXML_Node *node) {
        //auto s = ixmlNodetoString(node);
        //LOG_DEBUG(MEDIA_INDEXER_UPNP, "Item node: '%s'", s);
        auto id = getAttributeText(node, "id");
        if (!id)
            return;
        auto upnpClass = getNodeText(node, "upnp:class");
        if (!upnpClass) {
            free(id);
            return;
        }
        auto hash = generateItemHash(node);
        auto mime = upnpClassToMime(upnpClass);
        if (mime.empty()) {
            free(id);
            return;
        }
        auto obs = device->observer();
        /// @todo This is unsafe, obs might be reset after getting it
        if (!obs) {
            free(id);
            return;
        }
        MediaItemPtr mi(new MediaItem(device,
                std::string(id), mime, hash));
        obs->newMediaItem(std::move(mi));
        free(id);
    };
    iterateOnTag(doc, "item", handleItems);

    return true;
}

bool Upnp::parseMetaDataResponse(IXML_Document *doc,
    MediaItem &mediaItem) const
{
    switch (mediaItem.type()) {
    case MediaItem::Type::Audio:
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Title);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::DateOfCreation);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Genre);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Album);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Artist);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Track);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Duration);
        break;
    case MediaItem::Type::Video:
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Title);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::DateOfCreation);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Genre);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Duration);
        break;
    case MediaItem::Type::Image:
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::Title);
        setMetaOnMediaItem(doc, mediaItem, MediaItem::Meta::DateOfCreation);
        break;
    case MediaItem::Type::EOL:
        std::abort();
    }

    return true;
}

void Upnp::setMetaOnMediaItem(IXML_Document *doc, MediaItem &mediaItem,
    MediaItem::Meta meta) const
{
    const char *attrName = nullptr;
    const DOMString s = nullptr;

    switch (meta) {
    case MediaItem::Meta::Title:
        attrName = "dc:title";
        break;
    case MediaItem::Meta::Genre:
        attrName = "upnp:genre";
        break;
    case MediaItem::Meta::Album:
        attrName = "upnp:album";
        break;
    case MediaItem::Meta::Artist:
        [[fallthrough]]
    case MediaItem::Meta::AlbumArtist:
        attrName = "upnp:artist";
        break;
    case MediaItem::Meta::Track:
        attrName = "upnp:originalTrackNumber";
        break;
    case MediaItem::Meta::TotalTracks:
        // not part of DLNA content directory service specification
        break;
    case MediaItem::Meta::DateOfCreation:
        attrName = "dc:date"; // should be ISO8601 format - leave it
                              // like this
        break;
    case MediaItem::Meta::Duration: {
        auto resElem = ixmlDocument_getElementById(doc, "res");
        if (resElem)
            s = ixmlElement_getAttribute(resElem, "duration");
        break;
    }
    case MediaItem::Meta::GeoLocLongitude:
        [[fallthrough]]
    case MediaItem::Meta::GeoLocLatitude:
        [[fallthrough]]
    case MediaItem::Meta::GeoLocCountry:
        [[fallthrough]]
    case MediaItem::Meta::GeoLocCity:
        /// @todo: Maybe use file extractor with http uri to extract
        /// geo tags.
        break;
    case MediaItem::Meta::EOL:
        std::abort();
    }

    if (!s && attrName) {
        s = getNodeText(doc, attrName);
        if (!s || !strlen(s))
            return;
    } else if (!s) {
        return;
    }

    switch (meta) {
    case MediaItem::Meta::Title:
        [[fallthrough]]
    case MediaItem::Meta::Genre:
        [[fallthrough]]
    case MediaItem::Meta::Album:
        [[fallthrough]]
    case MediaItem::Meta::Artist:
        [[fallthrough]]
    case MediaItem::Meta::AlbumArtist:
        mediaItem.setMeta(meta, s);
        break;
    case MediaItem::Meta::Track:
        mediaItem.setMeta(meta, std::int64_t(std::atoi(s)));
        break;
    case MediaItem::Meta::DateOfCreation:
        mediaItem.setMeta(meta, s);
        break;
    case MediaItem::Meta::Duration:
    {
        int hours, minutes, seconds;
        std::sscanf(s, "%i:%i:%i", &hours, &minutes, &seconds);
        int64_t inSeconds = static_cast<int64_t>(hours * 60 + minutes) * 60 + seconds;
        mediaItem.setMeta(meta, inSeconds);
        break;
    }
    case MediaItem::Meta::EOL:
        std::abort();
    }
}

unsigned long Upnp::generateItemHash(IXML_Node *item) const
{
    unsigned long ret = 0;
    // do we have a rawDate attribute in <item>?
    auto rd = getAttributeText(item, "rawDate");
    if (rd) {
        ret = std::stoul(rd);
        free(rd);
        return ret;
    }

    // do we have a size field in <res>?
    auto resNode = getChildNode(item, "res");
    if (resNode) {
        auto sz = getAttributeText(item, "size");
        if (sz) {
            ret = std::stoul(sz);
            free(sz);
            return ret;
        }

        // no size field - do we have a duration?
        auto dur = getAttributeText(item, "duration");
        if (dur) {
            ret = std::hash<std::string>{}(dur);
            free(dur);
            return ret;
        }
    }

    // last resort - let's choose the item id
    auto id = getAttributeText(item, "id");
    ret = std::hash<std::string>{}(id);
    free(id);
    return ret;
}

std::string Upnp::upnpClassToMime(const std::string &upnpClass) const
{
    if (upnpClass == "object.item.audioItem.musicTrack")
        return "audio";
    if (upnpClass == "object.item.imageItem.photo")
        return "image";
    if (upnpClass == "object.item.videoItem")
        return "video";

    return "";
}

bool Upnp::upnpClassCheck(const std::string &upnpClass) const
{
    // audio
    if (upnpClass == "object.container.album.musicAlbum")
        return false;
    if (upnpClass == "object.container.person.musicArtist")
        return false;
    if (upnpClass == "object.container.gerne.musicGenre")
        return false;

    // video
    if (upnpClass == "object.container.person.movieActor")
        return false;
    if (upnpClass == "object.container.gerne.videoGenre")
        return false;

    return true;
}

IXML_Document *Upnp::getObjectMeta(const std::string &id,
    std::shared_ptr<Device> device) const
{
    IXML_Document *action;
    action = UpnpMakeAction("Browse", Upnp::upnpServiceCategory_, 6,
        "ObjectID", id.c_str(),
        "BrowseFlag", "BrowseMetadata",
        "Filter", "*",
        "StartingIndex", "0",
        "RequestedCount", "0",
        "SortCriteria", "");

    if (!action)
        return nullptr;

    IXML_Document *resp = nullptr;
    {
        std::lock_guard<std::mutex> lock(browseLock_);

        auto err = UpnpSendAction(upnpHandle_, device->mountpoint().c_str(),
            Upnp::upnpServiceCategory_, NULL, action, &resp);

        // we don't need this anymore
        ixmlDocument_free(action);

        if (err != UPNP_E_SUCCESS || !resp) {
            LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "Failed to send action to '%s': %i",
                device->uri().c_str(), err);
            if (resp)
                ixmlDocument_free(resp);
            return nullptr;
        }
    }

    // how many matches did we receive
    auto num = std::stoi(getNodeText(resp, "NumberReturned"));

    // if there are no matches skip this
    auto total = std::stoi(getNodeText(resp, "TotalMatches"));

    if (!total || !num) {
        ixmlDocument_free(resp);
        return nullptr;
    }

    // now get the DIDL text
    auto didl = getNodeText(resp, "Result");
    if (!didl) {
        LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "No browse DIDL result");
        ixmlDocument_free(resp);
        return nullptr;
    }
    // parse it into its own document
    ixmlRelaxParser(1);
    IXML_Document *didlDoc = ixmlParseBuffer(didl);
    if (!didlDoc) {
        LOG_ERROR(MEDIA_INDEXER_UPNP, 0, "DIDL parsing failed: %s", didl);
        ixmlDocument_free(resp);
        return nullptr;
    }

    ixmlDocument_free(resp);

    return didlDoc;
}
