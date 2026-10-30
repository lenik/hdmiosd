/*
 * Copyright (C) 2026 Lenik <hdmiosd@bodz.net>
 *
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <bas/log/deflog.h>
#include <bas/proc/AssetsRegistry.hpp>
#include <bas/proc/DefAssets.hpp>

define_logger();
define_zip_assets(hdmiosd, hdmiosd_assets);

struct HdmiosdAssetsRegistrar {
    HdmiosdAssetsRegistrar() { AssetsRegistry::pushLayer(hdmiosd_assets); }
} hdmiosd_assets_registrar;
