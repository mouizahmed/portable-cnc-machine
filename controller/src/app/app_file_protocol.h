#pragma once

#include "protocol/app_cdc_transport.h"

bool app_file_protocol_handle_command(AppCdcTransport& transport, const AppCdcTransport::Frame& frame);
