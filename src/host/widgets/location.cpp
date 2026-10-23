// SPDX-License-Identifier: GPL-3.0-or-later
// MacWin - Copyright (C) 2026 IMoggsYou. Created by IMoggsYou: https://github.com/IMoggsYou/MacWin

#include "host/widgets/location.h"
#include <windows.h>
#include <roapi.h>
#include <winstring.h>
// MinGW's windows.foundation.h specializes IReference<> for both BYTE and boolean, which are
// the same type in C++: the second one (unused here) is left out.
#define ____FIReference_1_boolean_INTERFACE_DEFINED__
#include <windows.devices.geolocation.h>
#include <math.h>
#include <stdio.h>
#include <wctype.h>
#include "host/com.h"
#include "host/json.h"

namespace widgets {

namespace {

const LONGLONG kTicksPerSecond = 10000000;   // TimeSpan counts 100 ns

}  // namespace

LocationStatus WindowsLocation(double* latitude, double* longitude, double* accuracy, HttpCancel* cancel) {
    using namespace ABI::Windows::Devices::Geolocation;
    using namespace ABI::Windows::Foundation;
    HSTRING_HEADER header;
    HSTRING name = nullptr;
    if (FAILED(WindowsCreateStringReference(RuntimeClass_Windows_Devices_Geolocation_Geolocator,
                                            (UINT32)wcslen(RuntimeClass_Windows_Devices_Geolocation_Geolocator),
                                            &header, &name))) {
        return LocationStatus::Unavailable;
    }
    ComPtr<IInspectable> instance;
    ComPtr<IGeolocator> locator;
    if (FAILED(RoActivateInstance(name, instance.put())) || FAILED(instance.As(&locator))) {
        return LocationStatus::Unavailable;
    }
    TimeSpan maxAge = {30 * 60 * kTicksPerSecond}, timeout = {10 * kTicksPerSecond};
    ComPtr<IAsyncOperation<Geoposition*>> operation;
    HRESULT hr = locator->GetGeopositionAsyncWithAgeAndTimeout(maxAge, timeout, operation.put());
    if (hr == E_ACCESSDENIED) return LocationStatus::Denied;
    ComPtr<IAsyncInfo> info;
    if (FAILED(hr) || FAILED(operation.As(&info))) return LocationStatus::Unavailable;
    // A worker thread: waiting here costs nothing, and shutting down can cut it short.
    AsyncStatus status = Started;
    for (int i = 0; i < 120 && SUCCEEDED(info->get_Status(&status)) && status == Started; ++i) {
        if (cancel->Cancelled()) break;
        Sleep(100);
    }
    if (status != Completed) {
        HRESULT error = S_OK;
        info->get_ErrorCode(&error);
        if (status == Started) info->Cancel();
        return error == E_ACCESSDENIED ? LocationStatus::Denied : LocationStatus::Unavailable;
    }
    ComPtr<IGeoposition> position;
    ComPtr<IGeocoordinate> coordinate;
    if (FAILED(operation->GetResults(position.put())) || !position ||
        FAILED(position->get_Coordinate(coordinate.put())) || !coordinate ||
        FAILED(coordinate->get_Latitude(latitude)) || FAILED(coordinate->get_Longitude(longitude))) {
        return LocationStatus::Unavailable;
    }
    if (FAILED(coordinate->get_Accuracy(accuracy))) *accuracy = 0;
    return LocationStatus::Found;
}

double RoundCoordinate(double degrees) {
    return round(degrees * 100) / 100;
}

double DistanceKm(double lat1, double lon1, double lat2, double lon2) {
    const double rad = 3.14159265358979 / 180;
    double dLat = (lat2 - lat1) * rad, dLon = (lon2 - lon1) * rad;
    double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * rad) * cos(lat2 * rad) * sin(dLon / 2) * sin(dLon / 2);
    return 6371 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

std::wstring PlaceNameUrl(double latitude, double longitude, const std::wstring& language) {
    // zoom 12: the town or village, not the street (in a big city, its name is still "city").
    wchar_t url[256];
    swprintf(url, ARRAYSIZE(url),
             L"https://nominatim.openstreetmap.org/reverse?format=jsonv2&zoom=12&lat=%.2f&lon=%.2f&accept-language=",
             latitude, longitude);
    std::wstring out = url;
    for (wchar_t c : language) {
        if (iswalnum(c) || c == L'-') out.push_back(c);
    }
    return out;
}

bool ParsePlaceName(const std::wstring& text, std::wstring* name) {
    json::Value root;
    if (!json::Parse(text, &root) || root.type != json::Value::Object) return false;
    const json::Value* address = root.Get(L"address");
    // Big or small, the place people would call it by.
    for (const wchar_t* key : {L"city", L"town", L"village", L"municipality", L"hamlet", L"suburb", L"county"}) {
        const json::Value* v = address && address->type == json::Value::Object ? address->Get(key) : nullptr;
        if (v && v->type == json::Value::String && !v->str.empty()) {
            *name = v->str;
            return true;
        }
    }
    const json::Value* fallback = root.Get(L"name");
    if (fallback && fallback->type == json::Value::String && !fallback->str.empty()) {
        *name = fallback->str;
        return true;
    }
    return false;
}

}  // namespace widgets
