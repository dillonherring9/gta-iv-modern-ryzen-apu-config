// The device thread (threaded=1): D3D9 calls recorded on the render thread and run by DXVK on a worker.
// The includer provides LogRecord, telemetryOn, renderTid, rsOwnCreate and WriteSlot; tools/perfproof/dt_test.cpp includes
// this file unchanged against a fake device.
#pragma once
static bool WriteSlot(void** vtbl, int slot, void* fn);
#include "dt_core.h"
#include "dt_shadow.h"
#include "dt_sbtab.h"       // the state-block table and its recorded Apply
#include "dt_device_rec.h"
#include "dt_device_sync.h"
#include "dt_res.h"
#include "dt_install.h"
