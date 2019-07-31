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


#ifdef LWM2M_SUPPORT_JSON

#define PRV_JSON_BUFFER_SIZE 1024

#define JSON_MIN_ARRAY_LEN      21      // e":[{"n":"N","v":X}]}
#define JSON_MIN_BASE_LEN        7      // n":"N",
#define JSON_ITEM_MAX_SIZE      36      // with ten characters for value
#define JSON_MIN_BX_LEN          5      // bt":1

#define JSON_FALSE_STRING  "false"
#define JSON_TRUE_STRING   "true"

#define JSON_OBJECT_HEADER                '{'
#define JSON_PARAM_HEADER                 "\"e\":["
#define JSON_PARAM_HEADER_SIZE            5
#define JSON_FOOTER                       "]}"
#define JSON_FOOTER_SIZE                  2
#define JSON_SEPARATOR                    ','


#define _GO_TO_NEXT_CHAR(I,B,L)         \
    {                                   \
        I++;                            \
        I += json_skipSpace(B+I, L-I);   \
        if (I == L) goto error;         \
    }

static int prv_parseItem(const uint8_t * buffer,
                         size_t bufferLen,
                         senml_record_t * recordP)
{
    size_t index;

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

        next = json_split(buffer+index, bufferLen-index, &tokenStart, &tokenLen, &valueStart, &valueLen);
        if (next < 0) return -1;

        switch (tokenLen)
        {
        case 1:
        {
            switch (buffer[index+tokenStart])
            {
            case 'n':
            {
                size_t i;
                size_t j;

                if (recordP->ids[0] != LWM2M_MAX_ID) return -1;

                // Check for " around URI
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                // Ignore starting /
                if (buffer[index + valueStart + 1] == '/')
                {
                    if (valueLen < 4)
                    {
                        return -1;
                    }
                    valueStart += 1;
                    valueLen -= 1;
                }
                i = 0;
                j = 0;
                if (valueLen > 1)
                {
                    do {
                        uint32_t readId;

                        readId = 0;
                        i++;
                        while (i < valueLen-1 && buffer[index+valueStart+i] != '/')
                        {
                            if (buffer[index+valueStart+i] < '0'
                             || buffer[index+valueStart+i] > '9')
                            {
                                return -1;
                            }
                            readId *= 10;
                            readId += buffer[index+valueStart+i] - '0';
                            if (readId > LWM2M_MAX_ID) return -1;
                            i++;
                        }
                        recordP->ids[j] = readId;
                        j++;
                    } while (i < valueLen-1 && j < 4 && buffer[index+valueStart+i] == '/');
                    if (i < valueLen-1 ) return -1;
                }
            }
            break;

            case 'v':
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                if (!json_convertNumeric(buffer+index+valueStart, valueLen, &recordP->value))
                    return -1;
                break;

            case 't':
                // TODO: support time
                break;

            default:
                return -1;
            }
        }
        break;

        case 2:
        {
            // "bv", "ov", or "sv"
            if (buffer[index+tokenStart+1] != 'v') return -1;
            switch (buffer[index+tokenStart])
            {
            case 'b':
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
                break;

            case 'o':
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                // Check for " around value
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
                break;

            case 's':
                if (recordP->value.type != LWM2M_TYPE_UNDEFINED) return -1;
                // Check for " around value
                if (valueLen < 2
                 || buffer[index+valueStart] != '"'
                 || buffer[index+valueStart+valueLen-1] != '"')
                {
                    return -1;
                }
                /* Don't use lwm2m_data_encode_nstring here. It would copy the buffer */
                recordP->value.type = LWM2M_TYPE_STRING;
                recordP->value.value.asBuffer.buffer = (uint8_t *)buffer + index + valueStart + 1;
                recordP->value.value.asBuffer.length = valueLen - 2;
                break;

            default:
                return -1;
            }
        }
        break;

        default:
            return -1;
        }

        index += next + 1;
    } while (index < bufferLen);

    return 0;
}

int json_parse(lwm2m_uri_t * uriP,
               const uint8_t * buffer,
               size_t bufferLen,
               lwm2m_data_t ** dataP)
{
    size_t index;
    int count = 0;
    bool eFound = false;
    bool bnFound = false;
    bool btFound = false;
    size_t bnStart;
    size_t bnLen;
    senml_record_t * recordArray;

    LOG_ARG("bufferLen: %d, buffer: \"%s\"", bufferLen, (char *)buffer);
    LOG_URI(uriP);
    *dataP = NULL;
    recordArray = NULL;

    index = json_skipSpace(buffer, bufferLen);
    if (index == bufferLen) return -1;

    if (buffer[index] != '{') return -1;
    do
    {
        _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
        if (buffer[index] != '"') goto error;
        if (index++ >= bufferLen) goto error;
        switch (buffer[index])
        {
        case 'e':
        {
            int recordIndex;

            if (bufferLen-index < JSON_MIN_ARRAY_LEN) goto error;
            index++;
            if (buffer[index] != '"') goto error;
            if (eFound == true) goto error;
            eFound = true;

            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            if (buffer[index] != ':') goto error;
            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            if (buffer[index] != '[') goto error;
            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            count = json_countItems(buffer + index, bufferLen - index);
            if (count <= 0) goto error;
            recordArray = (senml_record_t*)lwm2m_malloc(count * sizeof(senml_record_t));
            if (recordArray == NULL) goto error;
            // at this point we are sure buffer[index] is '{' and all { and } are matching
            recordIndex = 0;
            while (recordIndex < count)
            {
                int itemLen = json_itemLength(buffer + index, bufferLen - index);
                if (itemLen < 0) goto error;
                if (0 != prv_parseItem(buffer + index + 1, itemLen - 2, recordArray + recordIndex))
                {
                    goto error;
                }
                recordIndex++;
                index += itemLen - 1;
                _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
                switch (buffer[index])
                {
                case ',':
                    _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
                    break;
                case ']':
                    if (recordIndex == count) break;
                    // else this is an error
                default:
                    goto error;
                }
            }
            if (buffer[index] != ']') goto error;
        }
        break;

        case 'b':
            if (bufferLen-index < JSON_MIN_BX_LEN) goto error;
            index++;
            switch (buffer[index])
            {
            case 't':
                index++;
                if (buffer[index] != '"') goto error;
                if (btFound == true) goto error;
                btFound = true;

                // TODO: handle timed values
                // temp: skip this token
                while(index < bufferLen && buffer[index] != ',' && buffer[index] != '}') index++;
                if (index == bufferLen) goto error;
                index--;
                // end temp
                break;
            case 'n':
                {
                    int next;
                    size_t tokenStart;
                    size_t tokenLen;
                    int itemLen;

                    index++;
                    if (buffer[index] != '"') goto error;
                    if (bnFound == true) goto error;
                    bnFound = true;
                    index -= 3;
                    itemLen = 0;
                    while (buffer[index + itemLen] != '}'
                        && buffer[index + itemLen] != ','
                        && index + itemLen < bufferLen)
                    {
                        itemLen++;
                    }
                    if (index + itemLen == bufferLen) goto error;
                    next = json_split(buffer+index, itemLen, &tokenStart, &tokenLen, &bnStart, &bnLen);
                    if (next < 0) goto error;
                    bnStart += index;
                    index += next - 1;
                }
                break;
            default:
                goto error;
            }
            break;

        default:
            goto error;
        }

        _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
    } while (buffer[index] == ',');

    if (buffer[index] != '}') goto error;

    if (eFound == true)
    {
        lwm2m_uri_t baseURI;
        lwm2m_uri_t * baseUriP;

        LWM2M_URI_RESET(&baseURI);
        if (bnFound == false)
        {
            baseUriP = uriP;
        }
        else
        {
            int res;

            // we ignore the request URI and use the bn one.

            // Check for " around URI
            if (bnLen < 3
             || buffer[bnStart] != '"'
             || buffer[bnStart+bnLen-1] != '"')
            {
                goto error;
            }
            bnStart += 1;
            bnLen -= 2;

            if (bnLen == 1)
            {
                if (buffer[bnStart] != '/') goto error;
                baseUriP = NULL;
            }
            else
            {
                /* Base name may have a trailing "/" on a multiple instance
                 * resource. This isn't valid for a URI string in LWM2M 1.0.
                 * Strip off any trailing "/" to avoid an error. */
                if (buffer[bnStart + bnLen - 1] == '/') bnLen -= 1;
                res = lwm2m_stringToUri((char *)buffer + bnStart, bnLen, &baseURI);
                if (res < 0 || (size_t)res != bnLen) goto error;
                baseUriP = &baseURI;
            }
        }

        if (baseUriP)
        {
            int i;
            for (i = 0; i < count; i++)
            {
                if (LWM2M_URI_IS_SET_RESOURCE(baseUriP))
                {
                    recordArray[i].ids[3] = recordArray[i].ids[0];
                    recordArray[i].ids[2] = baseUriP->resourceId;
                    recordArray[i].ids[1] = baseUriP->instanceId;
                    recordArray[i].ids[0] = baseUriP->objectId;
                }
                else if (LWM2M_URI_IS_SET_INSTANCE(baseUriP))
                {
                    recordArray[i].ids[3] = recordArray[i].ids[1];
                    recordArray[i].ids[2] = recordArray[i].ids[0];
                    recordArray[i].ids[1] = baseUriP->instanceId;
                    recordArray[i].ids[0] = baseUriP->objectId;
                }
                else
                {
                    recordArray[i].ids[3] = recordArray[i].ids[2];
                    recordArray[i].ids[2] = recordArray[i].ids[1];
                    recordArray[i].ids[1] = recordArray[i].ids[0];
                    recordArray[i].ids[0] = baseUriP->objectId;
                }
            }
        }

        count = senml_convert_records(uriP, recordArray, count, json_convertValue, dataP);
        recordArray = NULL;

        if (count > 0)
        {
            LOG_ARG("Parsing successful. count: %d", count);
            return count;
        }
    }

error:
    LOG("Parsing failed");
    if (recordArray != NULL)
    {
        lwm2m_free(recordArray);
    }
    return -1;
}

int json_serialize(lwm2m_uri_t * uriP,
                   int size,
                   lwm2m_data_t * tlvP,
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
    bool baseNameOutput = true;
#ifndef LWM2M_VERSION_1_0
    lwm2m_uri_t uri;
#endif

    LOG_ARG("size: %d", size);
    LOG_URI(uriP);
    if (size != 0 && tlvP == NULL) return -1;

#ifndef LWM2M_VERSION_1_0
    if (uriP && LWM2M_URI_IS_SET_RESOURCE_INSTANCE(uriP))
    {
        /* The resource instance doesn't get serialized as part of the base URI.
         * Strip it out. */
        memcpy(&uri, uriP, sizeof(lwm2m_uri_t));
        uri.resourceInstanceId = LWM2M_MAX_ID;
        uriP = &uri;
    }
#endif

    baseUriLen = uri_toString(uriP, baseUriStr, URI_MAX_STRING_LEN, &baseLevel);
    if (baseUriLen < 0) return -1;

    num = senml_findAndCheckData(uriP, baseLevel, size, tlvP, &targetP, &rootLevel);
    if (num < 0) return -1;

    if (baseLevel >= URI_DEPTH_RESOURCE
     && rootLevel == baseLevel
     && baseUriLen > 1)
    {
        /* Remove the ID from the base name */
        while (baseUriLen > 1 && baseUriStr[baseUriLen - 1] != '/') baseUriLen--;
        if (baseUriLen > 1 && baseUriStr[baseUriLen - 1] == '/') baseUriLen--;
    }

    while (num == 1
        && (targetP->type == LWM2M_TYPE_OBJECT
         || targetP->type == LWM2M_TYPE_OBJECT_INSTANCE
         || targetP->type == LWM2M_TYPE_MULTIPLE_RESOURCE))
    {
        if (baseUriLen >= URI_MAX_STRING_LEN -1) return 0;
        baseUriStr[baseUriLen++] = '/';
        res = utils_intToText(targetP->id, baseUriStr + baseUriLen, URI_MAX_STRING_LEN - baseUriLen);
        if (res <= 0) return 0;
        baseUriLen += res;
        num = targetP->value.asChildren.count;
        targetP = targetP->value.asChildren.array;
    }

    bufferJSON[0] = JSON_OBJECT_HEADER;
    head = 1;

    if (baseUriLen > 0)
    {
        baseUriStr[baseUriLen++] = '/';
        res = json_serializeBaseName(baseUriStr, baseUriLen, bufferJSON + head, PRV_JSON_BUFFER_SIZE - head);
        if (res <= 0) return -1;
        head += res;
        if (PRV_JSON_BUFFER_SIZE - head < 1) return -1;
        bufferJSON[head++] = JSON_SEPARATOR;
    }
    else
    {
        parentUriStr = (const uint8_t *)"/";
        parentUriLen = 1;
    }
    if (PRV_JSON_BUFFER_SIZE - head < JSON_PARAM_HEADER_SIZE) return -1;
    memcpy(bufferJSON + head, JSON_PARAM_HEADER, JSON_PARAM_HEADER_SIZE);

    for (index = 0 ; index < num && head < PRV_JSON_BUFFER_SIZE ; index++)
    {
        res = json_serializeData(targetP + index,
                                 NULL,
                                 0,
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

    if (head + JSON_FOOTER_SIZE > PRV_JSON_BUFFER_SIZE) return 0;
    memcpy(bufferJSON + head, JSON_FOOTER, JSON_FOOTER_SIZE);
    head = head + JSON_FOOTER_SIZE;

    *bufferP = (uint8_t *)lwm2m_malloc(head);
    if (*bufferP == NULL) return -1;
    memcpy(*bufferP, bufferJSON, head);

    return head;
}

#endif

