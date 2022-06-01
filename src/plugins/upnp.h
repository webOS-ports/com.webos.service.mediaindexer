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

#pragma once

#include "plugin.h"
#include "logging.h"

#include <upnp.h>

#include <functional>

/// UPnP plugin class definition.
class Upnp : public Plugin
{
public:
    /// Base uri of this plugin.
    static std::string uri;

    /**
     * \brief Get UPnP plugin singleton instance.
     *
     * \return Singleton object in std::shared_ptr.
     */
    static std::shared_ptr<Plugin> instance();

    /// We cannot make this private because it is used from std::make_shared()
    Upnp();
    virtual ~Upnp();

    /**
     * \brief Export client handle.
     *
     * \return Client handle.
     */
    UpnpClient_Handle upnpHandle(void) const;

    /// Use standard device scan method
    void scan(const std::string &uri);

    /// From base class.
    void extractMeta(MediaItem &mediaItem, bool expand = false);

    /// From base class.
    std::optional<std::string> getPlaybackUri(const std::string &uri);

private:
    /// Singleton object.
    static std::shared_ptr<Plugin> instance_;
    /// Type of UPnP devices we are looking for.
    static const char *upnpDeviceCategory_;
    /// Service of UPnP devices we are looking for.
    static const char *upnpServiceCategory_;
    /// Time in seconds to wait for responses.
    static const int upnpSearchTimeout_;

    /// UPnP event callback.
    static int eventCallback(Upnp_EventType eventType, const void *event,
        void *cookie);

    /**
     * \brief Get result DIDL from action response.
     *
     * \param[in] event The action completed event.
     * \return The DIDL document or nullptr.
     */
    static IXML_Document *actionResult(UpnpActionComplete *event);

    /**
     * \brief Get device meta data and push it into the device.
     *
     * \param[in] plugin The plugin instance.
     * \param[in] uri The device uri.
     * \param[in] location The location information from discovery.
     * response.
     */
    static void getDeviceMeta(Upnp *plugin, std::string uri,
        std::string location);

    /// From plugin base class.
    int runDeviceDetection(bool start);

    /**
     * Register newly found service.
     *
     * \param[in] event A DISCOVERY event.
     */
    void serviceFound(const void *event);

    /**
     * Called if service says byebye.
     *
     * \param[in] event A DISCOVERY event.
     */
    void serviceLost(const void *event);

    /**
     * \brief Replace whitespace and ':' with '-', remove leading
     * 'uuid:'.
     *
     * \param[in] deviceId The unmangled DeviceID from discovery reponse.
     * \return The mangled string.
     */
    std::string mangleUri(const UpnpString *deviceId);

    /**
     * \brief Get child node from node.
     *
     * \param[in] node The IXML node structure.
     * \param[in] tagName The tag name.
     * \return The node or nullptr if no text found.
     */
    IXML_Node *getChildNode(IXML_Node *node, const char *tagName) const; 

    /**
     * \brief Get text content from node.
     *
     * The node must be unique within the document.
     *
     * \param[in] doc The IXML document structure.
     * \param[in] tagName The tag name.
     * \return The text or nullptr if no text found.
     */
    const DOMString getNodeText(IXML_Document *doc, const char *tagName) const;

    /**
     * \brief Get text content from node.
     *
     * \param[in] node The node structure.
     * \param[in] tagName The tag name.
     * \return The text or nullptr if no text found.
     */
    const DOMString getNodeText(IXML_Node *node, const char *tagName) const;

    /**
     * \brief Get text content from node.
     *
     * \param[in] node The node structure.
     * \return The text or nullptr if no text found.
     */
    const DOMString getNodeText(IXML_Node *node) const;

    /**
     * \brief Get text content from an attribute.
     *
     * Caller has to free returned memory.
     *
     * \param[in] node The node structure.
     * \param[in] attrName The name of the attribute.
     * \return The text or nullptr if no text found.
     */
    DOMString getAttributeText(IXML_Node *node, const char *attrName) const;

    /**
     * \brief Get text content from node.
     *
     * The node must be unique within the document.
     *
     * \param[in] doc The IXML document structure.
     * \param[in] tagName The tag name.
     * \param[in] func Function to call within iteration.
     */
    void iterateOnTag(IXML_Document *doc, const char *tagName,
        const std::function<void(IXML_Node*)> &func) const;

    /**
     * \brief Check if service category matches.
     *
     * The caller must free the returned Url.
     *
     * \param[in] doc The IXML document structure.
     * \return The control Url if matching service found, else nullptr.
     */
    const char *checkServiceCategory(IXML_Document *doc) const;

    /**
     * \brief Request meta data for object.
     *
     * \param[in] id The object id.
     * \param[in] mediaItem The media item to request meta data for.
     * \return True on success, else false.
     */
    bool sendMetaRequest(const std::string &id, MediaItem &mediaItem) const;

    /**
     * \brief Send browse request.
     *
     * \param[in] id The object id.
     * \param[in] count The expected number of matches.
     * \param[in] device The device to browse.
     * \return True on success, else false.
     */
    bool sendBrowseRequest(const std::string &id, int count,
        std::shared_ptr<Device> device) const;

    /**
     * \brief Browse chunk of items.
     *
     * \param[in] id The object id.
     * \param[in] start The start index.
     * \param[in] count The number of items to request.
     * \param[in] device The device to browse for scan mode.
     * \return The number of items found or -1 on error.
     */
    int browseChunk(const std::string &id, int start, int count,
        std::shared_ptr<Device> device) const;

    /**
     * \brief Handle browse response.
     *
     * \param[in] doc The DIDL doc.
     * \param[in] device The device.
     * \return True on success, else false.
     */
    bool parseBrowseResponse(IXML_Document *doc,
        std::shared_ptr<Device> device) const;

    /**
     * \brief Handle meta data response.
     *
     * \param[in] doc The DIDL doc.
     * \param[in] mediaItem The mediaItem to parse into.
     * \return True on success, else false.
     */
    bool parseMetaDataResponse(IXML_Document *doc,
        MediaItem &mediaItem) const;

    /**
     * \brief Read the requested meta from upnp response.
     *
     * \param[in] doc The DIDL doc.
     * \param[in] mediaItem The media item.
     * \param[in] meta The meta tag.
     */
    void setMetaOnMediaItem(IXML_Document *doc, MediaItem &mediaItem,
        MediaItem::Meta meta) const;

    /**
     * \brief Try to generate hash from item tag.
     *
     * \param[in] item The item to hash.
     * \return The hash value.
     */
    unsigned long generateItemHash(IXML_Node *item) const;

    /**
     * \brief Get the meta data of an opject.
     *
     * If a DIDL document is returned it must be free'd using
     * ixmlDocument_free().
     *
     * \param[in] id The object id.
     * \param[in] device The device to work on.
     * \return The DIDL document or nullptr.
     */
    IXML_Document *getObjectMeta(const std::string &id,
        std::shared_ptr<Device> device) const;

    /**
     * \brief Generate simple MIME identifier from UPNP class type.
     *
     * \param[in] upnpClass The UPNP class string.
     * \return A MIME string.
     */
    std::string upnpClassToMime(const std::string &upnpClass) const;

    /**
     * \brief Check if we want to dive into this type of container.
     *
     * \param[in] upnpClass The UPNP class string.
     * \return True or false.
     */
    bool upnpClassCheck(const std::string &upnpClass) const;

    /// UPnP client handle.
    UpnpClient_Handle upnpHandle_;

    /// List of upnp devices know to not be ContentDir providers.
    std::list<std::string> blacklist_;

    /// Lock for the blacklist.
    mutable std::mutex blacklistLock_;

    /// Lock for concurrent server access.
    mutable std::mutex browseLock_;
};
