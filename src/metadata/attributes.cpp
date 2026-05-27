// Copyright (c) 2022 Samsung Electronics Co., LTD
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include "metadata/attributes.h"

#include <functional>
#include <algorithm>
#include <cstdint>

#include "metadata/typeprinter.h"


namespace netcoredbg
{

static bool ForEachAttribute(IMetaDataImport *pMD, mdToken tok, std::function<HRESULT(const std::string &AttrName)> cb)
{
    bool found = false;
    ULONG numAttributes = 0;
    HCORENUM fEnum = NULL;
    mdCustomAttribute attr;
    while(SUCCEEDED(pMD->EnumCustomAttributes(&fEnum, tok, 0, &attr, 1, &numAttributes)) && numAttributes != 0)
    {
        std::string mdName;
        mdToken ptkObj = mdTokenNil;
        mdToken ptkType = mdTokenNil;
        if (FAILED(pMD->GetCustomAttributeProps(attr, &ptkObj, &ptkType, nullptr, nullptr)) ||
            FAILED(TypePrinter::NameForToken(ptkType, pMD, mdName, true, nullptr)))
            continue;

        found = cb(mdName);
        if (found)
            break;
    }
    pMD->CloseEnum(fEnum);
    return found;
}

bool HasAttribute(IMetaDataImport *pMD, mdToken tok, const char *attrName)
{
    return ForEachAttribute(pMD, tok, [&attrName](const std::string &AttrName) -> bool
    {
        return AttrName == attrName;
    });
}

bool HasAttribute(IMetaDataImport *pMD, mdToken tok, std::vector<std::string> &attrNames)
{
    return ForEachAttribute(pMD, tok, [&attrNames](const std::string &AttrName) -> bool
    {
        return std::find(attrNames.begin(), attrNames.end(), AttrName) != attrNames.end();
    });
}

static bool ReadCompressedUInt(const BYTE *&pData, const BYTE *pEnd, uint32_t &value)
{
    if (pData >= pEnd)
        return false;

    BYTE b1 = *pData++;
    if ((b1 & 0x80) == 0)
    {
        value = b1;
        return true;
    }

    if ((b1 & 0xC0) == 0x80)
    {
        if (pEnd - pData < 1)
            return false;

        value = ((b1 & 0x3F) << 8) | *pData++;
        return true;
    }

    if ((b1 & 0xE0) == 0xC0)
    {
        if (pEnd - pData < 3)
            return false;

        value = ((b1 & 0x1F) << 24) |
                (static_cast<uint32_t>(pData[0]) << 16) |
                (static_cast<uint32_t>(pData[1]) << 8) |
                pData[2];
        pData += 3;
        return true;
    }

    return false;
}

static bool DecodeCustomAttributeStringArgument(const void *pBlob, ULONG cbSize, std::string &value)
{
    const BYTE *pData = static_cast<const BYTE *>(pBlob);
    const BYTE *pEnd = pData + cbSize;

    if (pEnd - pData < 2 || pData[0] != 0x01 || pData[1] != 0x00)
        return false;

    pData += 2;

    if (pData >= pEnd)
        return false;

    // SerString null marker. DebuggerDisplayAttribute.Value should not use it,
    // but treat it as an empty fixed argument instead of reading past the blob.
    if (*pData == 0xFF)
    {
        value.clear();
        return true;
    }

    uint32_t stringLength = 0;
    if (!ReadCompressedUInt(pData, pEnd, stringLength) || static_cast<uint32_t>(pEnd - pData) < stringLength)
        return false;

    value.assign(reinterpret_cast<const char *>(pData), stringLength);
    return true;
}

bool GetAttributeStringArgument(IMetaDataImport *pMD, mdToken tok, const char *attrName, std::string &value)
{
    ULONG numAttributes = 0;
    HCORENUM fEnum = NULL;
    mdCustomAttribute attr;
    while(SUCCEEDED(pMD->EnumCustomAttributes(&fEnum, tok, 0, &attr, 1, &numAttributes)) && numAttributes != 0)
    {
        mdToken ptkObj = mdTokenNil;
        mdToken ptkType = mdTokenNil;
        void const *pBlob = nullptr;
        ULONG cbSize = 0;
        if (FAILED(pMD->GetCustomAttributeProps(attr, &ptkObj, &ptkType, &pBlob, &cbSize)))
            continue;

        std::string mdName;
        if (FAILED(TypePrinter::NameForToken(ptkType, pMD, mdName, true, nullptr)))
            continue;

        if (mdName == attrName && DecodeCustomAttributeStringArgument(pBlob, cbSize, value))
        {
            pMD->CloseEnum(fEnum);
            return true;
        }
    }
    pMD->CloseEnum(fEnum);
    return false;
}

} // namespace netcoredbg
