/*******************************************************************************
 *
 * Copyright (c) 2015 Intel Corporation and others.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the Eclipse Public License v2.0
 * and Eclipse Distribution License v1.0 which accompany this distribution.
 *
 * The Eclipse Public License is available at
 *    http://www.eclipse.org/legal/epl-v20.html
 * The Eclipse Distribution License is available at
 *    http://www.eclipse.org/org/documents/edl-v10.php.
 *
 * Contributors:
 *    David Navarro, Intel Corporation - initial API and implementation
 *    Scott Bertin, AMETEK, Inc. - Please refer to git log
 *
 *******************************************************************************/


#include "internals.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>


#ifdef LWM2M_SUPPORT_SENML_JSON

#ifdef LWM2M_VERSION_1_0
#error SenML JSON not supported with LWM2M 1.0
#endif

#define PRV_JSON_BUFFER_SIZE 1024

#define JSON_FALSE_STRING                 "false"
#define JSON_TRUE_STRING                  "true"

#define JSON_ITEM_BEGIN                   '{'
#define JSON_ITEM_END                     '}'

#define JSON_HEADER                       '['
#define JSON_FOOTER                       ']'
#define JSON_SEPARATOR                    ','


#define _GO_TO_NEXT_CHAR(I,B,L)         \
    {                                   \
        I++;                            \
        I += json_skipSpace(B+I, L-I);   \
        if (I == L) goto error;         \
    }

static int prv_parseItem(const uint8_t * buffer,
                         size_t bufferLen,
                         senml_record_t * recordP,
                         char * baseUri,
                         time_t * baseTime,
                         lwm2m_data_t *baseValue)
{
    size_t index;
    const uint8_t *name = NULL;
    size_t nameLength = 0;
    bool timeSeen = false;
    bool bnSeen = false;
    bool btSeen = false;
    bool bvSeen = false;
    bool bverSeen = false;

    recordP->ids[0] = LWM2M_MAX_ID;
    recordP->ids[1] = LWM2M_MAX_ID;
    recordP->ids[2] = LWM2M_MAX_ID;
    recordP->ids[3] = LWM2M_MAX_ID;
    memset(&recordP->value, 0, sizeof(recordP->value));
    recordP->value.id = LWM2M_MAX_ID;
    recordP->time = 0;

    index = 0;
    do
    {
        size_t tokenStart;
        size_t tokenLen;
        size_t valueStart;
        size_t valueLen;
        int next;

        next = json_split(buffer+index,
                          bufferLen-index,
                          &tokenStart,
                          &tokenLen,
                          &valueStart,
                          &valueLen);
        if (next < 0) return -1;
        if (tokenLen == 0) return -1;

        switch (buffer[index+tokenStart])
        {
        case 'b':
            if (tokenLen == 2 && buffer[index+tokenStart+1] == 'n')
            {
                if (bnSeen) return -1;
                bnSeen = true;
                /* Check for " around URI */
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                if (valueLen >= 3)
                {
                    if (valueLen == 3 && buffer[index+valueStart+1] != '/') return -1;
                    if (valueLen > URI_MAX_STRING_LEN) return -1;
                    memcpy(baseUri, buffer+index+valueStart+1, valueLen-2);
                    baseUri[valueLen-2] = '\0';
                }
                else
                {
                    baseUri[0] = '\0';
                }
            }
            else if (tokenLen == 2 && buffer[index+tokenStart+1] == 't')
            {
                if (btSeen) return -1;
                btSeen = true;
                if (!json_convertTime(buffer+index+valueStart, valueLen, baseTime))
                    return -1;
            }
            else if (tokenLen == 2 && buffer[index+tokenStart+1] == 'v')
            {
                if (bvSeen) return -1;
                bvSeen = true;
                if (valueLen == 0)
                {
                    baseValue->type = LWM2M_TYPE_UNDEFINED;
                }
                else
                {
                    if (!json_convertNumeric(buffer+index+valueStart, valueLen, baseValue))
                        return -1;
                    /* Convert explicit 0 to implicit 0 */
                    switch (baseValue->type)
                    {
                    case LWM2M_TYPE_INTEGER:
                        if (baseValue->value.asInteger == 0)
                        {
                            baseValue->type = LWM2M_TYPE_UNDEFINED;
                        }
                        break;
                    case LWM2M_TYPE_UNSIGNED_INTEGER:
                        if (baseValue->value.asUnsigned == 0)
                        {
                            baseValue->type = LWM2M_TYPE_UNDEFINED;
                        }
                        break;
                    case LWM2M_TYPE_FLOAT:
                        if (baseValue->value.asFloat == 0.0)
                        {
                            baseValue->type = LWM2M_TYPE_UNDEFINED;
                        }
                        break;
                    default:
                        return -1;
                    }
                }
            }
            else if (tokenLen == 4
                  && buffer[index+tokenStart+1] == 'v'
                  && buffer[index+tokenStart+2] == 'e'
                  && buffer[index+tokenStart+3] == 'r')
            {
                int64_t value;
                int res;
                if (bverSeen) return -1;
                bverSeen = true;
                res = utils_textToInt(buffer+index+valueStart, valueLen, &value);
                /* Only the default version (10) is supported */
                if (!res || value != 10)
                {
                    return -1;
                }
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        case 'n':
        {
            if (tokenLen == 1)
            {
                if (name) return -1;

                /* Check for " around URI */
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                name = buffer + index + valueStart + 1;
                nameLength = valueLen - 2;
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        }
        case 't':
            if (tokenLen == 1)
            {
                if (timeSeen) return -1;
                timeSeen = true;
                if (!json_convertTime(buffer+index+valueStart, valueLen, &recordP->time))
                    return -1;
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        case 'v':
            if (tokenLen == 1)
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                if (!json_convertNumeric(buffer+index+valueStart, valueLen, &recordP->value))
                    return -1;
            }
            else if (tokenLen == 2 && buffer[index+tokenStart+1] == 'b')
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                if (0 == lwm2m_strncmp(JSON_TRUE_STRING,
                                       (char *)buffer + index + valueStart,
                                       valueLen))
                {
                    lwm2m_data_encode_bool(true, &recordP->value);
                }
                else if (0 == lwm2m_strncmp(JSON_FALSE_STRING,
                                            (char *)buffer + index + valueStart,
                                            valueLen))
                {
                    lwm2m_data_encode_bool(false, &recordP->value);
                }
                else
                {
                    return -1;
                }
            }
            else if (tokenLen == 2
                  && (buffer[index+tokenStart+1] == 'd'
                   || buffer[index+tokenStart+1] == 's'))
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                /* Check for " around value */
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                if (buffer[index+tokenStart+1] == 'd')
                {
                    /* Don't use lwm2m_data_encode_opaque here. It would copy the buffer */
                    recordP->value.type = LWM2M_TYPE_OPAQUE;
                }
                else
                {
                    /* Don't use lwm2m_data_encode_nstring here. It would copy the buffer */
                    recordP->value.type = LWM2M_TYPE_STRING;
                }
                recordP->value.value.asBuffer.buffer = (uint8_t *)buffer + index + valueStart + 1;
                recordP->value.value.asBuffer.length = valueLen - 2;
            }
            else if (tokenLen == 3 && buffer[index+tokenStart+1] == 'l' && buffer[index+tokenStart+2] == 'o')
            {
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                /* Check for " around value */
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                if (!utils_textToObjLink(buffer + index + valueStart + 1,
                                         valueLen - 2,
                                         &recordP->value.value.asObjLink.objectId,
                                         &recordP->value.value.asObjLink.objectInstanceId))
                {
                    return -1;
                }
                recordP->value.type = LWM2M_TYPE_OBJECT_LINK;
            }
            else if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        default:
            if (buffer[index+tokenStart+tokenLen-1] == '_')
            {
                /* Label ending in _ must be supported or generate error. */
                return -1;
            }
            break;
        }

        index += next + 1;
    } while (index < bufferLen);

    /* Combine with base values */
    recordP->time += *baseTime;
    if (baseUri[0] || name)
    {
        lwm2m_uri_t uri;
        size_t length = strlen(baseUri);
        char uriStr[URI_MAX_STRING_LEN];
        if (length > sizeof(uriStr)) return -1;
        memcpy(uriStr, baseUri, length);
        if (nameLength)
        {
            if (nameLength + length > sizeof(uriStr)) return -1;
            memcpy(uriStr + length, name, nameLength);
            length += nameLength;
        }
        if (!lwm2m_stringToUri(uriStr, length, &uri)) return -1;
        if (LWM2M_URI_IS_SET_OBJECT(&uri))
        {
            recordP->ids[0] = uri.objectId;
        }
        if (LWM2M_URI_IS_SET_INSTANCE(&uri))
        {
            recordP->ids[1] = uri.instanceId;
        }
        if (LWM2M_URI_IS_SET_RESOURCE(&uri))
        {
            recordP->ids[2] = uri.resourceId;
        }
        if (LWM2M_URI_IS_SET_RESOURCE_INSTANCE(&uri))
        {
            recordP->ids[3] = uri.resourceInstanceId;
        }
    }
    if (baseValue->type != LWM2M_TYPE_UNDEFINED)
    {
        if (recordP->value.type == LWM2M_TYPE_UNDEFINED)
        {
            memcpy(&recordP->value, baseValue, sizeof(*baseValue));
        }
        else
        {
            switch (recordP->value.type)
            {
            case LWM2M_TYPE_INTEGER:
                switch(baseValue->type)
                {
                case LWM2M_TYPE_INTEGER:
                    recordP->value.value.asInteger += baseValue->value.asInteger;
                    break;
                case LWM2M_TYPE_UNSIGNED_INTEGER:
                    recordP->value.value.asInteger += baseValue->value.asUnsigned;
                    break;
                case LWM2M_TYPE_FLOAT:
                    recordP->value.value.asInteger += (int64_t)baseValue->value.asFloat;
                    break;
                default:
                    return -1;
                }
                break;
            case LWM2M_TYPE_UNSIGNED_INTEGER:
                switch(baseValue->type)
                {
                case LWM2M_TYPE_INTEGER:
                    recordP->value.value.asUnsigned += baseValue->value.asInteger;
                    break;
                case LWM2M_TYPE_UNSIGNED_INTEGER:
                    recordP->value.value.asUnsigned += baseValue->value.asUnsigned;
                    break;
                case LWM2M_TYPE_FLOAT:
                    recordP->value.value.asUnsigned += (uint64_t)baseValue->value.asFloat;
                    break;
                default:
                    return -1;
                }
                break;
            case LWM2M_TYPE_FLOAT:
                switch(baseValue->type)
                {
                case LWM2M_TYPE_INTEGER:
                    recordP->value.value.asFloat += baseValue->value.asInteger;
                    break;
                case LWM2M_TYPE_UNSIGNED_INTEGER:
                    recordP->value.value.asFloat += baseValue->value.asUnsigned;
                    break;
                case LWM2M_TYPE_FLOAT:
                    recordP->value.value.asFloat += baseValue->value.asFloat;
                    break;
                default:
                    return -1;
                }
                break;
            default:
                return -1;
            }
        }
    }

    return 0;
}

int senml_json_parse(const lwm2m_uri_t * uriP,
                     const uint8_t * buffer,
                     size_t bufferLen,
                     lwm2m_data_t ** dataP)
{
    size_t index;
    int count = 0;
    senml_record_t * recordArray;
    int recordIndex;
    char baseUri[URI_MAX_STRING_LEN + 1];
    time_t baseTime;
    lwm2m_data_t baseValue;

    LOG_ARG("bufferLen: %d, buffer: \"%s\"", bufferLen, (char *)buffer);
    LOG_URI(uriP);
    *dataP = NULL;
    recordArray = NULL;

    index = json_skipSpace(buffer, bufferLen);
    if (index == bufferLen) return -1;

    if (buffer[index] != JSON_HEADER) return -1;

    _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
    count = json_countItems(buffer + index, bufferLen - index);
    if (count <= 0) goto error;
    recordArray = (senml_record_t*)lwm2m_malloc(count * sizeof(senml_record_t));
    if (recordArray == NULL) goto error;
    /* at this point we are sure buffer[index] is '{' and all { and } are matching */
    recordIndex = 0;
    baseUri[0] = '\0';
    baseTime = 0;
    memset(&baseValue, 0, sizeof(baseValue));
    while (recordIndex < count)
    {
        int itemLen = json_itemLength(buffer + index, bufferLen - index);
        if (itemLen < 0) goto error;
        if (prv_parseItem(buffer + index + 1,
                          itemLen - 2,
                          recordArray + recordIndex,
                          baseUri,
                          &baseTime,
                          &baseValue))
        {
            goto error;
        }
        recordIndex++;
        index += itemLen - 1;
        _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
        switch (buffer[index])
        {
        case JSON_SEPARATOR:
            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            break;
        case JSON_FOOTER:
            if (recordIndex != count) goto error;
            break;
        default:
            goto error;
        }
    }

    if (buffer[index] != JSON_FOOTER) goto error;

    count = senml_convert_records(uriP, recordArray, count, json_convertValue, dataP);
    recordArray = NULL;

    if (count > 0)
    {
        LOG_ARG("Parsing successful. count: %d", count);
        return count;
    }

error:
    LOG("Parsing failed");
    if (recordArray != NULL)
    {
        lwm2m_free(recordArray);
    }
    return -1;
}

int senml_json_serialize(const lwm2m_uri_t * uriP,
                         int size,
                         const lwm2m_data_t * tlvP,
                         uint8_t ** bufferP)
{
    int index;
    size_t head;
    uint8_t bufferJSON[PRV_JSON_BUFFER_SIZE];
    uint8_t baseUriStr[URI_MAX_STRING_LEN];
    int baseUriLen;
    uri_depth_t rootLevel;
    uri_depth_t baseLevel;
    int num;
    int res;
    lwm2m_data_t * targetP;
    const uint8_t *parentUriStr = NULL;
    size_t parentUriLen = 0;

    LOG_ARG("size: %d", size);
    LOG_URI(uriP);
    if (size != 0 && tlvP == NULL) return -1;

    baseUriLen = uri_toString(uriP, baseUriStr, URI_MAX_STRING_LEN, &baseLevel);
    if (baseUriLen < 0) return -1;
    if (baseUriLen > 1
     && baseLevel != URI_DEPTH_RESOURCE
     && baseLevel != URI_DEPTH_RESOURCE_INSTANCE)
    {
        if (baseUriLen >= URI_MAX_STRING_LEN -1) return 0;
        baseUriStr[baseUriLen++] = '/';
    }

    num = senml_findAndCheckData(uriP, baseLevel, size, tlvP, &targetP, &rootLevel);
    if (num < 0) return -1;

    if (baseLevel < rootLevel
     && baseUriLen > 1
     && baseUriStr[baseUriLen - 1] != '/')
    {
        if (baseUriLen >= URI_MAX_STRING_LEN -1) return 0;
        baseUriStr[baseUriLen++] = '/';
    }

    if (!baseUriLen || baseUriStr[baseUriLen - 1] != '/')
    {
        parentUriStr = (const uint8_t *)"/";
        parentUriLen = 1;
    }

    head = 0;
    bufferJSON[head++] = JSON_HEADER;

    bool baseNameOutput = false;
    for (index = 0 ; index < num && head < PRV_JSON_BUFFER_SIZE ; index++)
    {
        if (index != 0)
        {
            if (head + 1 > PRV_JSON_BUFFER_SIZE) return 0;
            bufferJSON[head++] = JSON_SEPARATOR;
        }

        res = json_serializeData(targetP + index,
                                 baseUriStr,
                                 baseUriLen,
                                 baseLevel,
                                 parentUriStr,
                                 parentUriLen,
                                 rootLevel,
                                 &baseNameOutput,
                                 bufferJSON + head,
                                 PRV_JSON_BUFFER_SIZE - head);
        if (res < 0) return res;
        head += res;
    }

    if (!baseNameOutput && baseUriLen > 0)
    {
        // Remove trailing /
        if (baseUriLen > 1) baseUriLen -= 1;

        if (PRV_JSON_BUFFER_SIZE - head < 1) return -1;
        bufferJSON[head++] = JSON_ITEM_BEGIN;
        res = json_serializeBaseName(baseUriStr,
                                     baseUriLen,
                                     bufferJSON + head,
                                     PRV_JSON_BUFFER_SIZE - head);
        if (res <= 0) return -1;
        head += res;
        if (PRV_JSON_BUFFER_SIZE - head < 1) return -1;
        bufferJSON[head++] = JSON_ITEM_END;
        baseNameOutput = true;
    }

    if (head + 1 > PRV_JSON_BUFFER_SIZE) return 0;
    bufferJSON[head++] = JSON_FOOTER;

    *bufferP = (uint8_t *)lwm2m_malloc(head);
    if (*bufferP == NULL) return -1;
    memcpy(*bufferP, bufferJSON, head);

    return head;
}

#endif

