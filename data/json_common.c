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
#include <float.h>

#if defined(LWM2M_SUPPORT_JSON) || defined(LWM2M_SUPPORT_SENML_JSON)

#define JSON_FALSE_STRING                 "false"
#define JSON_FALSE_STRING_SIZE            5
#define JSON_TRUE_STRING                  "true"
#define JSON_TRUE_STRING_SIZE             4

#define JSON_ITEM_BEGIN                   '{'
#define JSON_ITEM_END                     '}'
#define JSON_ITEM_URI                     "\"n\":\""
#define JSON_ITEM_URI_SIZE                5
#define JSON_ITEM_URI_END                 '"'
#define JSON_ITEM_BOOL                    "\"vb\":"
#define JSON_ITEM_BOOL_SIZE               5
#define JSON_ITEM_NUM                     "\"v\":"
#define JSON_ITEM_NUM_SIZE                4
#define JSON_ITEM_STRING_BEGIN            "\"vs\":\""
#define JSON_ITEM_STRING_BEGIN_SIZE       6
#define JSON_ITEM_STRING_END              '"'
#define JSON_ITEM_OPAQUE_BEGIN            "\"vd\":\""
#define JSON_ITEM_OPAQUE_BEGIN_SIZE       6
#define JSON_ITEM_OPAQUE_END              '"'
#define JSON_ITEM_OBJECT_LINK_BEGIN       "\"vlo\":\""
#define JSON_ITEM_OBJECT_LINK_BEGIN_SIZE  7
#define JSON_ITEM_OBJECT_LINK_END         '"'

#define JSON_BN_HEADER                    "\"bn\":\""
#define JSON_BN_HEADER_SIZE               6
#define JSON_BN_END                       '"'
#define JSON_BT_HEADER                    "\"bt\":"
#define JSON_BT_HEADER_SIZE               5
#define JSON_SEPARATOR                    ','

#define _GO_TO_NEXT_CHAR(I,B,L)         \
    {                                   \
        I++;                            \
        I += json_skipSpace(B+I, L-I);   \
        if (I == L) goto error;         \
    }

typedef enum
{
    _STEP_START,
    _STEP_TOKEN,
    _STEP_ANY_SEPARATOR,
    _STEP_SEPARATOR,
    _STEP_QUOTED_VALUE,
    _STEP_VALUE,
    _STEP_DONE
} _itemState;

static int prv_isReserved(char sign)
{
    if (sign == '['
     || sign == '{'
     || sign == ']'
     || sign == '}'
     || sign == ':'
     || sign == ','
     || sign == '"')
    {
        return 1;
    }

    return 0;
}

static int prv_isWhiteSpace(uint8_t sign)
{
    if (sign == 0x20
     || sign == 0x09
     || sign == 0x0A
     || sign == 0x0D)
    {
        return 1;
    }

    return 0;
}

size_t json_skipSpace(const uint8_t * buffer,size_t bufferLen)
{
    size_t i;

    i = 0;
    while ((i < bufferLen)
        && prv_isWhiteSpace(buffer[i]))
    {
        i++;
    }

    return i;
}

int json_split(const uint8_t * buffer,
               size_t bufferLen,
               size_t * tokenStartP,
               size_t * tokenLenP,
               size_t * valueStartP,
               size_t * valueLenP)
{
    size_t index;
    _itemState step;

    *tokenStartP = 0;
    *tokenLenP = 0;
    *valueStartP = 0;
    *valueLenP = 0;
    index = 0;
    step = _STEP_START;

    index = json_skipSpace(buffer + index, bufferLen - index);
    if (index == bufferLen) return -1;

    while (index < bufferLen)
    {
        switch (step)
        {
        case _STEP_START:
            if (buffer[index] == ',') goto loop_exit;
            if (buffer[index] != '"') return -1;
            *tokenStartP = index+1;
            step = _STEP_TOKEN;
            break;

        case _STEP_TOKEN:
            if (buffer[index] == ',') goto loop_exit;
            if (buffer[index] == '"')
            {
                *tokenLenP = index - *tokenStartP;
                step = _STEP_ANY_SEPARATOR;
            }
            break;

        case _STEP_ANY_SEPARATOR:
            if (buffer[index] == ',') goto loop_exit;
            if (buffer[index] != ':') return -1;
            step = _STEP_SEPARATOR;
            break;

        case _STEP_SEPARATOR:
            if (buffer[index] == ',') goto loop_exit;
            if (buffer[index] == '"')
            {
                *valueStartP = index;
                step = _STEP_QUOTED_VALUE;
            } else if (!prv_isReserved(buffer[index]))
            {
                *valueStartP = index;
                step = _STEP_VALUE;
            } else
            {
                return -1;
            }
            break;

        case _STEP_QUOTED_VALUE:
            if (buffer[index] == '"' && buffer[index-1] != '\\' )
            {
                *valueLenP = index - *valueStartP + 1;
                step = _STEP_DONE;
            }
            break;

        case _STEP_VALUE:
            if (buffer[index] == ',') goto loop_exit;
            if (prv_isWhiteSpace(buffer[index]))
            {
                *valueLenP = index - *valueStartP;
                step = _STEP_DONE;
            }
            break;

        case _STEP_DONE:
            if (buffer[index] == ',') goto loop_exit;
            return -1;

        default:
            return -1;
        }

        index++;
        if (step == _STEP_START
         || step == _STEP_ANY_SEPARATOR
         || step == _STEP_SEPARATOR
         || step == _STEP_DONE)
        {
            index += json_skipSpace(buffer + index, bufferLen - index);
        }
    }
loop_exit:

    if (step == _STEP_VALUE)
    {
        *valueLenP = index - *valueStartP;
        step = _STEP_DONE;
    }

    if (step != _STEP_DONE) return -1;

    return (int)index;
}

int json_itemLength(const uint8_t * buffer, size_t bufferLen)
{
    size_t index;
    int in;

    index = 0;
    in = 0;

    while (index < bufferLen)
    {
        switch (in)
        {
        case 0:
            if (buffer[index] != '{') goto error;
            in = 1;
            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            break;
        case 1:
            if (buffer[index] == '{') goto error;
            if (buffer[index] == '}')
            {
                return index + 1;
            }
            else if (buffer[index] == '"')
            {
                in = 2;
                _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            }
            else
            {
                _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            }
            break;
        case 2:
            if (buffer[index] == '"')
            {
                in = 1;
            }
            else if (buffer[index] == '\\')
            {
                /* Escape in string. Skip the next character. */
                index++;
                if (index == bufferLen) goto error;
            }
            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
            break;
        default:
            goto error;
        }
    }

error:
    return -1;
}

int json_countItems(const uint8_t * buffer, size_t bufferLen)
{
    int count;
    size_t index;
    int in;

    count = 0;
    index = 0;
    in = 0;

    while (index < bufferLen)
    {
        int len = json_itemLength(buffer + index,  bufferLen - index);
        if (len <= 0) return -1;
        count++;
        index += len - 1;
        _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
        if (buffer[index] == ']')
        {
            /* Done processing */
            index = bufferLen;
        }
        else if (buffer[index] == ',')
        {
            _GO_TO_NEXT_CHAR(index, buffer, bufferLen);
        }
        else return -1;
    }
    if (in > 0) goto error;

    return count;

error:
    return -1;
}


int json_convertNumeric(const uint8_t *value,
                        size_t valueLen,
                        lwm2m_data_t *targetP)
{
    int result = 0;
    size_t i = 0;
    while (i < valueLen && value[i] != '.' && value[i] != 'e' && value[i] != 'E')
    {
        i++;
    }
    if (i == valueLen)
    {
        if (value[0] == '-')
        {
            int64_t val;

            result = utils_textToInt(value, valueLen, &val);
            if (result)
            {
                lwm2m_data_encode_int(val, targetP);
            }
        }
        else
        {
            uint64_t val;

            result = utils_textToUInt(value, valueLen, &val);
            if (result)
            {
                lwm2m_data_encode_uint(val, targetP);
            }
        }
    }
    else
    {
        double val;

        result = utils_textToFloat(value, valueLen, &val, true);
        if (result)
        {
            lwm2m_data_encode_float(val, targetP);
        }
    }

    return result;
}

int json_convertTime(const uint8_t *valueStart, size_t valueLen, time_t *t)
{
    int64_t value;
    int res = utils_textToInt(valueStart, valueLen, &value);
    if (res)
    {
        *t = (time_t)value;
    }
    return res;
}

static uint8_t prv_hexValue(uint8_t digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    if (digit >= 'A' && digit <= 'F') return digit - 'A' + 10;
    return 0xFF;
}

size_t json_unescapeString(uint8_t *dst, const uint8_t *src, size_t len)
{
    size_t i;
    size_t result = 0;
    for (i = 0; i < len; i++)
    {
        uint8_t c = src[i];
        if (c == '\\')
        {
            i++;
            if (i >= len) return false;
            c = src[i];
            switch (c)
            {
            case '"':
            case '\\':
            case '/':
                dst[result++] = (char)c;
                break;
            case 'b':
                dst[result++] = '\b';
                break;
            case 'f':
                dst[result++] = '\f';
                break;
            case 'n':
                dst[result++] = '\n';
                break;
            case 'r':
                dst[result++] = '\r';
                break;
            case 't':
                dst[result++] = '\t';
                break;
            case 'u':
            {
                uint8_t v1, v2;
                i++;
                if (i >= len - 4) return 0;
                if (src[i++] != '0') return 0;
                if (src[i++] != '0') return 0;
                v1 = prv_hexValue(src[i++]);
                if (v1 > 15) return 0;
                v2 = prv_hexValue(src[i]);
                if (v2 > 15) return 0;
                dst[result++] = (char)((v1 << 4) + v2);
                break;
            }
            default:
                /* invalid escape sequence */
                return 0;
            }
        }
        else
        {
            dst[result++] = (char)c;
        }
    }
    return result;
}


static uint8_t prv_hexDigit(uint8_t value)
{
    value = value & 0xF;
    if (value < 10) return '0' + value;
    return 'A' + value - 10;
}

size_t json_escapeString(uint8_t *dst, size_t dstLen, const uint8_t *src, size_t srcLen)
{
    size_t i;
    size_t head = 0;
    for (i = 0; i < srcLen; i++)
    {
        if (src[i] < 0x20)
        {
            if (src[i] == '\b')
            {
                if (dstLen - head < 2) return 0;
                dst[head++] = '\\';
                dst[head++] = 'b';
            }
            else if(src[i] == '\f')
            {
                if (dstLen - head < 2) return 0;
                dst[head++] = '\\';
                dst[head++] = 'f';
            }
            else if(src[i] == '\n')
            {
                if (dstLen - head < 2) return 0;
                dst[head++] = '\\';
                dst[head++] = 'n';
            }
            else if(src[i] == '\r')
            {
                if (dstLen - head < 2) return 0;
                dst[head++] = '\\';
                dst[head++] = 'r';
            }
            else if(src[i] == '\t')
            {
                if (dstLen - head < 2) return 0;
                dst[head++] = '\\';
                dst[head++] = 't';
            }
            else
            {
                if (dstLen - head < 6) return 0;
                dst[head++] = '\\';
                dst[head++] = 'u';
                dst[head++] = '0';
                dst[head++] = '0';
                dst[head++] = prv_hexDigit(src[i] >> 4);
                dst[head++] = prv_hexDigit(src[i]);
            }
        }
        else if (src[i] == '"')
        {
            if (dstLen - head < 2) return 0;
            dst[head++] = '\\';
            dst[head++] = '\"';
        }
        else if (src[i] == '\\')
        {
            if (dstLen - head < 2) return 0;
            dst[head++] = '\\';
            dst[head++] = '\\';
        }
        else
        {
            if (dstLen - head < 1) return 0;
            dst[head++] = src[i];
        }
    }

    return head;
}

bool json_convertValue(const senml_record_t * recordP,
                       lwm2m_data_t * targetP)
{
    switch (recordP->value.type)
    {
    case LWM2M_TYPE_STRING:
        if (0 != recordP->value.value.asBuffer.length)
        {
            size_t stringLen;
            uint8_t *string = (uint8_t *)lwm2m_malloc(recordP->value.value.asBuffer.length);
            if (!string) return false;
            stringLen = json_unescapeString(string,
                                            recordP->value.value.asBuffer.buffer,
                                            recordP->value.value.asBuffer.length);
            if (stringLen)
            {
                lwm2m_data_encode_nstring((char *)string, stringLen, targetP);
                lwm2m_free(string);
            }
            else
            {
                lwm2m_free(string);
                return false;
            }
        }
        else
        {
            lwm2m_data_encode_nstring(NULL, 0, targetP);
        }
        break;
    case LWM2M_TYPE_OPAQUE:
        if (0 != recordP->value.value.asBuffer.length)
        {
            size_t dataLength;
            uint8_t *data;
            dataLength = utils_base64GetDecodedSize((const char *)recordP->value.value.asBuffer.buffer,
                                                    recordP->value.value.asBuffer.length);
            data = lwm2m_malloc(dataLength);
            if (!data) return false;
            dataLength = utils_base64Decode((const char *)recordP->value.value.asBuffer.buffer,
                                   recordP->value.value.asBuffer.length,
                                   data,
                                   dataLength);
            if (dataLength)
            {
                lwm2m_data_encode_opaque(data, dataLength, targetP);
                lwm2m_free(data);
            }
            else
            {
                lwm2m_free(data);
                return false;
            }
        }
        else
        {
            lwm2m_data_encode_opaque(NULL, 0, targetP);
        }
        break;
    default:
        if (recordP->value.type != LWM2M_TYPE_UNDEFINED)
        {
            targetP->type = recordP->value.type;
        }
        memcpy(&targetP->value, &recordP->value.value, sizeof(targetP->value));
        break;
    case LWM2M_TYPE_OBJECT:
    case LWM2M_TYPE_OBJECT_INSTANCE:
    case LWM2M_TYPE_MULTIPLE_RESOURCE:
    case LWM2M_TYPE_CORE_LINK:
        /* Should never happen */
        return false;
    }

    return true;
}

int json_serializeBaseName(const uint8_t * baseUriStr,
                           size_t baseUriLen,
                           uint8_t * buffer,
                           size_t bufferLen)
{
    int head;

    if (bufferLen < JSON_BN_HEADER_SIZE) return -1;
    memcpy(buffer, JSON_BN_HEADER, JSON_BN_HEADER_SIZE);
    head = JSON_BN_HEADER_SIZE;

    if (bufferLen - head < baseUriLen) return -1;
    memcpy(buffer + head, baseUriStr, baseUriLen);
    head += baseUriLen;

    if (bufferLen - head < 1) return -1;
    buffer[head++] = JSON_BN_END;

    return head;
}

static int prv_serializeName(const uint8_t * parentUriStr,
                             size_t parentUriLen,
                             uint16_t id,
                             uint8_t * buffer,
                             size_t bufferLen)
{
    int head;
    int res;

    if (bufferLen < JSON_ITEM_URI_SIZE) return -1;
    memcpy(buffer, JSON_ITEM_URI, JSON_ITEM_URI_SIZE);
    head = JSON_ITEM_URI_SIZE;

    if (parentUriLen > 0)
    {
        if (bufferLen - head < parentUriLen) return -1;
        memcpy(buffer + head, parentUriStr, parentUriLen);
        head += parentUriLen;
    }

    res = utils_intToText(id, buffer + head, bufferLen - head);
    if (res <= 0) return -1;
    head += res;

    if (bufferLen - head < 1) return -1;
    buffer[head++] = JSON_ITEM_URI_END;

    return head;
}

static int prv_serializeValue(const lwm2m_data_t * tlvP,
                              uint8_t * buffer,
                              size_t bufferLen)
{
    size_t res;
    size_t head;

    switch (tlvP->type)
    {
    case LWM2M_TYPE_STRING:
    case LWM2M_TYPE_CORE_LINK:
        if (bufferLen < JSON_ITEM_STRING_BEGIN_SIZE) return -1;
        memcpy(buffer, JSON_ITEM_STRING_BEGIN, JSON_ITEM_STRING_BEGIN_SIZE);
        head = JSON_ITEM_STRING_BEGIN_SIZE;

        res = json_escapeString(buffer + head,
                                bufferLen - head,
                                tlvP->value.asBuffer.buffer,
                                tlvP->value.asBuffer.length);
        if (res < tlvP->value.asBuffer.length) return -1;
        head += res;

        if (bufferLen - head < 1) return -1;
        buffer[head++] = JSON_ITEM_STRING_END;

        break;

    case LWM2M_TYPE_INTEGER:
    {
        int64_t value;

        if (0 == lwm2m_data_decode_int(tlvP, &value)) return -1;

        if (bufferLen < JSON_ITEM_NUM_SIZE) return -1;
        memcpy(buffer, JSON_ITEM_NUM, JSON_ITEM_NUM_SIZE);
        head = JSON_ITEM_NUM_SIZE;

        res = utils_intToText(value, buffer + head, bufferLen - head);
        if (!res) return -1;
        head += res;
    }
    break;

    case LWM2M_TYPE_UNSIGNED_INTEGER:
    {
        uint64_t value;

        if (0 == lwm2m_data_decode_uint(tlvP, &value)) return -1;

        if (bufferLen < JSON_ITEM_NUM_SIZE) return -1;
        memcpy(buffer, JSON_ITEM_NUM, JSON_ITEM_NUM_SIZE);
        head = JSON_ITEM_NUM_SIZE;

        res = utils_uintToText(value, buffer + head, bufferLen - head);
        if (!res) return -1;
        head += res;
    }
    break;

    case LWM2M_TYPE_FLOAT:
    {
        double value;

        if (0 == lwm2m_data_decode_float(tlvP, &value)) return -1;

        if (bufferLen < JSON_ITEM_NUM_SIZE) return -1;
        memcpy(buffer, JSON_ITEM_NUM, JSON_ITEM_NUM_SIZE);
        head = JSON_ITEM_NUM_SIZE;

        res = utils_floatToText(value, buffer + head, bufferLen - head, true);
        if (!res) return -1;
        /* Error if inf or nan */
        if (buffer[head] != '-' && (buffer[head] < '0' || buffer[head] > '9')) return -1;
        if (res > 1 && buffer[head] == '-' && (buffer[head+1] < '0' || buffer[head+1] > '9')) return -1;
        head += res;
    }
    break;

    case LWM2M_TYPE_BOOLEAN:
    {
        bool value;

        if (0 == lwm2m_data_decode_bool(tlvP, &value)) return -1;

        if (value)
        {
            if (bufferLen < JSON_ITEM_BOOL_SIZE + JSON_TRUE_STRING_SIZE) return -1;
            memcpy(buffer,
                   JSON_ITEM_BOOL JSON_TRUE_STRING,
                   JSON_ITEM_BOOL_SIZE + JSON_TRUE_STRING_SIZE);
            head = JSON_ITEM_BOOL_SIZE + JSON_TRUE_STRING_SIZE;
        }
        else
        {
            if (bufferLen < JSON_ITEM_BOOL_SIZE + JSON_FALSE_STRING_SIZE) return -1;
            memcpy(buffer,
                   JSON_ITEM_BOOL JSON_FALSE_STRING,
                   JSON_ITEM_BOOL_SIZE + JSON_FALSE_STRING_SIZE);
            head = JSON_ITEM_BOOL_SIZE + JSON_FALSE_STRING_SIZE;
        }
    }
    break;

    case LWM2M_TYPE_OPAQUE:
        if (bufferLen < JSON_ITEM_OPAQUE_BEGIN_SIZE) return -1;
        memcpy(buffer, JSON_ITEM_OPAQUE_BEGIN, JSON_ITEM_OPAQUE_BEGIN_SIZE);
        head = JSON_ITEM_OPAQUE_BEGIN_SIZE;

        if (tlvP->value.asBuffer.length > 0)
        {
            res = utils_base64Encode(tlvP->value.asBuffer.buffer,
                                     tlvP->value.asBuffer.length,
                                     buffer+head,
                                     bufferLen - head);
            if (res < tlvP->value.asBuffer.length) return -1;
            head += res;
        }

        if (bufferLen - head < 1) return -1;
        buffer[head++] = JSON_ITEM_OPAQUE_END;
        break;

    case LWM2M_TYPE_OBJECT_LINK:
        if (bufferLen < JSON_ITEM_OBJECT_LINK_BEGIN_SIZE) return -1;
        memcpy(buffer,
               JSON_ITEM_OBJECT_LINK_BEGIN,
               JSON_ITEM_OBJECT_LINK_BEGIN_SIZE);
        head = JSON_ITEM_OBJECT_LINK_BEGIN_SIZE;

        res = utils_objLinkToText(tlvP->value.asObjLink.objectId,
                                  tlvP->value.asObjLink.objectInstanceId,
                                  buffer + head,
                                  bufferLen - head);
        if (!res) return -1;
        head += res;

        if (bufferLen - head < 1) return -1;
        buffer[head++] = JSON_ITEM_OBJECT_LINK_END;
        break;

    default:
        return -1;
    }

    return (int)head;
}

static int prv_serializeTlv(const lwm2m_data_t * tlvP,
                            const uint8_t * baseUriStr,
                            size_t baseUriLen,
                            uri_depth_t baseLevel,
                            const uint8_t * parentUriStr,
                            size_t parentUriLen,
                            uri_depth_t level,
                            bool *baseNameOutput,
                            uint8_t * buffer,
                            size_t bufferLen)
{
    int res;
    int head = 0;
    bool needSeparator = false;

    // Start the record
    if (bufferLen < 1) return -1;
    buffer[head++] = JSON_ITEM_BEGIN;

    if (!*baseNameOutput && baseUriLen > 0)
    {
        res = json_serializeBaseName(baseUriStr,
                                    baseUriLen,
                                    buffer + head,
                                    bufferLen - head);
        if (res <= 0) return -1;
        head += res;
        *baseNameOutput = true;
        needSeparator = true;
    }

    /* TODO: support base time */

    if (!baseUriLen || level > baseLevel)
    {
        if (needSeparator)
        {
            if (bufferLen < 1) return -1;
            buffer[head++] = JSON_SEPARATOR;
        }
        res = prv_serializeName(parentUriStr,
                                parentUriLen,
                                tlvP->id,
                                buffer + head,
                                bufferLen - head);
        if (res <= 0) return -1;
        head += res;
        needSeparator = true;
    }

    switch (tlvP->type)
    {
    case LWM2M_TYPE_UNDEFINED:
    case LWM2M_TYPE_OBJECT:
    case LWM2M_TYPE_OBJECT_INSTANCE:
    case LWM2M_TYPE_MULTIPLE_RESOURCE:
        // no value
        break;
    default:
        if (needSeparator)
        {
            if (bufferLen < 1) return -1;
            buffer[head++] = JSON_SEPARATOR;
        }
        res = prv_serializeValue(tlvP, buffer + head, bufferLen - head);
        if (res < 0) return -1;
        head += res;
        break;
    }

    /* TODO: support time */

    // End the record
    if (bufferLen < 1) return -1;
    buffer[head++] = JSON_ITEM_END;

    return head;
}

int json_serializeData(const lwm2m_data_t * tlvP,
                       const uint8_t * baseUriStr,
                       size_t baseUriLen,
                       uri_depth_t baseLevel,
                       const uint8_t * parentUriStr,
                       size_t parentUriLen,
                       uri_depth_t level,
                       bool *baseNameOutput,
                       uint8_t * buffer,
                       size_t bufferLen)
{
    int res;
    int head = 0;

    switch (tlvP->type)
    {
    case LWM2M_TYPE_MULTIPLE_RESOURCE:
    case LWM2M_TYPE_OBJECT:
    case LWM2M_TYPE_OBJECT_INSTANCE:
    {
        if (tlvP->value.asChildren.count == 0)
        {
            res = prv_serializeTlv(tlvP,
                                   baseUriStr,
                                   baseUriLen,
                                   baseLevel,
                                   parentUriStr,
                                   parentUriLen,
                                   level,
                                   baseNameOutput,
                                   buffer + head,
                                   bufferLen - head);
            if (res <= 0) return -1;
            head += res;
        }
        else
        {
            uint8_t uriStr[URI_MAX_STRING_LEN];
            size_t uriLen;
            size_t index;

            if (parentUriLen > 0)
            {
                if (URI_MAX_STRING_LEN < parentUriLen) return -1;
                memcpy(uriStr, parentUriStr, parentUriLen);
                uriLen = parentUriLen;
            }
            else
            {
                uriLen = 0;
            }
            res = utils_intToText(tlvP->id,
                                  uriStr + uriLen,
                                  URI_MAX_STRING_LEN -1 - uriLen);
            if (res <= 0) return -1;
            uriLen += res;
            uriStr[uriLen] = '/';
            uriLen++;

            for (index = 0 ; index < tlvP->value.asChildren.count; index++)
            {
                if (index != 0)
                {
                    if (bufferLen - head < 1) return -1;
                    buffer[head++] = JSON_SEPARATOR;
                }

                res = json_serializeData(tlvP->value.asChildren.array + index,
                                        baseUriStr,
                                        baseUriLen,
                                        baseLevel,
                                        uriStr,
                                        uriLen,
                                        level,
                                        baseNameOutput,
                                        buffer + head,
                                        bufferLen - head);
                if (res < 0) return -1;
                head += res;
            }
        }
    }
    break;

    default:
        res = prv_serializeTlv(tlvP,
                               baseUriStr,
                               baseUriLen,
                               baseLevel,
                               parentUriStr,
                               parentUriLen,
                               level,
                               baseNameOutput,
                               buffer + head,
                               bufferLen - head);
        if (res <= 0) return -1;
        head += res;
        break;
    }

    return head;
}

#endif
