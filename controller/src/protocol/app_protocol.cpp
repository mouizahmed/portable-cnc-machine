#include "protocol/app_protocol.h"

#include <string.h>

#include "app/app_file_protocol.h"
#include "app/app_bmp280_sensor.h"
#include "app/app_job_service.h"
#include "app/app_job_stream.h"
#include "app/app_machine_state.h"
#include "app/app_motion_control.h"
#include "app/app_system_command.h"
#include "machine/machine_status.h"
#include "protocol/app_cdc_transport.h"
#include "protocol/protocol_defs.h"

#include "grbl/core_handlers.h"

extern "C" {
#include "grbl/errors.h"
#include "grbl/hal.h"
#include "grbl/task.h"
}

static AppCdcTransport transport;
static AppCdcTransport::Frame rx_frame;
static unsigned long last_job_progress_line = 0;
static const char *last_bmp280_status_message = nullptr;

static void copy_fixed(char *dest, size_t dest_size, const char *src)
{
    if(dest == nullptr || dest_size == 0)
        return;

    memset(dest, 0, dest_size);

    if(src == nullptr)
        return;

    const size_t len = strnlen(src, dest_size - 1);
    memcpy(dest, src, len);
}

static ProtocolMachineState protocol_state_from_mode(app_machine_mode_t mode)
{
    switch(mode) {
        case AppMachineMode_Idle:
            return MACHINE_IDLE;
        case AppMachineMode_Jogging:
            return MACHINE_JOG;
        case AppMachineMode_RunningJob:
            return MACHINE_RUNNING;
        case AppMachineMode_Holding:
            return MACHINE_HOLD;
        case AppMachineMode_Alarm:
            return MACHINE_FAULT;
        default:
            return MACHINE_IDLE;
    }
}

static void send_response(uint32_t request_seq, const void *payload, uint16_t payload_len)
{
    transport.send_frame(FRAME_RESP, PCNC_TRANSFER_ID_NONE, request_seq,
                         (const uint8_t *)payload, payload_len);
}

static void send_event(const void *payload, uint16_t payload_len)
{
    transport.send_frame(FRAME_EVENT, PCNC_TRANSFER_ID_NONE, 0,
                         (const uint8_t *)payload, payload_len);
}

static void send_message_event(uint8_t level, const char *message)
{
    EventMessage event = {};
    event.message_type = EVENT_MESSAGE;
    event.message_level = level;
    copy_fixed(event.message, sizeof(event.message), message);
    send_event(&event, sizeof(event));
}

static void send_error(uint32_t request_seq, ProtocolErrorCode error, const char *reason)
{
    RespError response = {};
    response.message_type = RESP_ERROR;
    response.request_seq = request_seq;
    response.error = (uint8_t)error;
    copy_fixed(response.reason, sizeof(response.reason), reason);
    send_response(request_seq, &response, sizeof(response));
}

static void send_job_error_event(const char *reason)
{
    EventJobError event = {};
    event.message_type = EVENT_JOB_ERROR;
    copy_fixed(event.reason, sizeof(event.reason), reason);
    send_event(&event, sizeof(event));
}

static void send_job_complete_event(void)
{
    EventJobComplete event = {};
    event.message_type = EVENT_JOB_COMPLETE;
    send_event(&event, sizeof(event));
}

static void send_job_progress_event(uint32_t line, uint32_t total)
{
    EventJobProgress event = {};
    event.message_type = EVENT_JOB_PROGRESS;
    event.line = line;
    event.total = total;
    send_event(&event, sizeof(event));
}

static void send_command_ack(uint32_t request_seq, uint8_t command_type)
{
    RespCommandAck response = {};
    response.message_type = RESP_COMMAND_ACK;
    response.request_seq = request_seq;
    response.command_type = command_type;
    send_response(request_seq, &response, sizeof(response));
}

static void soft_reset_task(void *data)
{
    (void)data;

    if(grbl.enqueue_realtime_command != nullptr)
        grbl.enqueue_realtime_command(0x18u);
}

static void send_caps(uint32_t request_seq)
{
    RespCaps response = {};
    response.message_type = RESP_CAPS;
    response.request_seq = request_seq;
    response.caps = CAP_MOTION |
                    CAP_FILE_LOAD |
                    CAP_JOB_START |
                    CAP_JOB_PAUSE |
                    CAP_JOB_RESUME |
                    CAP_JOB_ABORT |
                    CAP_RESET;
    send_response(request_seq, &response, sizeof(response));
}

static void send_safety(uint32_t request_seq)
{
    RespSafety response = {};
    response.message_type = RESP_SAFETY;
    response.request_seq = request_seq;
    response.safety = SAFETY_MONITORING;
    send_response(request_seq, &response, sizeof(response));
}

static void send_temperature(uint32_t request_seq)
{
    RespTemperature response = {};
    float temperature_c = 0.0f;
    response.message_type = RESP_TEMPERATURE;
    response.request_seq = request_seq;
    response.has_temperature = app_bmp280_sensor_temperature_c(&temperature_c) ? 1 : 0;
    response.temperature_c = temperature_c;
    send_response(request_seq, &response, sizeof(response));
}

static void send_job(uint32_t request_seq)
{
    RespJob response = {};
    response.message_type = RESP_JOB;
    response.request_seq = request_seq;
    response.has_job = app_job_service_has_job() ? 1 : 0;
    copy_fixed(response.name, sizeof(response.name), app_job_service_name());
    send_response(request_seq, &response, sizeof(response));
}

static void send_position(uint32_t request_seq)
{
    machine_status_snapshot_t snapshot;
    machine_status_snapshot(&snapshot);

    RespPos response = {};
    response.message_type = RESP_POS;
    response.request_seq = request_seq;
    response.mx = snapshot.mpos_x;
    response.my = snapshot.mpos_y;
    response.mz = snapshot.mpos_z;
    response.wx = snapshot.mpos_x;
    response.wy = snapshot.mpos_y;
    response.wz = snapshot.mpos_z;
    send_response(request_seq, &response, sizeof(response));
}

static void send_state(uint32_t request_seq)
{
    machine_status_snapshot_t snapshot;
    machine_status_snapshot(&snapshot);

    RespState response = {};
    response.message_type = RESP_STATE;
    response.request_seq = request_seq;
    response.state = (uint8_t)protocol_state_from_mode(snapshot.mode);
    send_response(request_seq, &response, sizeof(response));
}

static void send_info(uint32_t request_seq)
{
    RespInfo response = {};
    response.message_type = RESP_INFO;
    response.request_seq = request_seq;
    copy_fixed(response.firmware, sizeof(response.firmware), "grblHAL");
    copy_fixed(response.board, sizeof(response.board), "Teensy 4.1");
    send_response(request_seq, &response, sizeof(response));
}

static void send_status_bundle(uint32_t request_seq)
{
    send_state(request_seq);
    send_caps(request_seq);
    send_safety(request_seq);
    send_job(request_seq);
    send_position(request_seq);
    send_temperature(request_seq);
}

static void handle_command_frame(const AppCdcTransport::Frame& frame)
{
    if(frame.transfer_id != PCNC_TRANSFER_ID_NONE || frame.payload_len == 0) {
        send_error(frame.seq, ERROR_UNKNOWN, "Invalid command frame");
        return;
    }

    if(app_file_protocol_handle_command(transport, frame))
        return;

    switch(frame.payload[0]) {
        case PCNC_CMD_PING: {
            RespPong response = {};
            response.message_type = RESP_PONG;
            response.request_seq = frame.seq;
            send_response(frame.seq, &response, sizeof(response));
            break;
        }

        case PCNC_CMD_INFO:
            send_info(frame.seq);
            break;

        case PCNC_CMD_STATUS:
            send_status_bundle(frame.seq);
            break;

        case PCNC_CMD_JOG: {
            if(frame.payload_len != sizeof(CmdJog)) {
                send_error(frame.seq, ERROR_MISSING_PARAM, "Invalid jog command");
                break;
            }

            CmdJog command = {};
            memcpy(&command, frame.payload, sizeof(command));
            app_motion_result_t result = app_motion_jog(command.axis, command.dist, command.feed);
            if(result == AppMotionResult_Ok)
                send_command_ack(frame.seq, frame.payload[0]);
            else
                send_error(frame.seq, ERROR_INVALID_STATE, app_motion_result_reason(result));
            break;
        }

        case PCNC_CMD_JOG_CANCEL: {
            app_motion_result_t result = app_motion_jog_cancel();
            if(result == AppMotionResult_Ok)
                send_command_ack(frame.seq, frame.payload[0]);
            else
                send_error(frame.seq, ERROR_INVALID_STATE, app_motion_result_reason(result));
            break;
        }

        case PCNC_CMD_START:
            if(!app_job_service_has_job()) {
                send_error(frame.seq, ERROR_NO_JOB_LOADED, "No job loaded");
            } else {
                if(app_job_service_is_running()) {
                    if(app_job_stream_resume())
                        send_command_ack(frame.seq, frame.payload[0]);
                    else
                        send_error(frame.seq, ERROR_INVALID_STATE, "Could not resume job");
                    break;
                }

                char path[PCNC_MAX_FILENAME_BYTES + 2] = {};
                copy_fixed(path, sizeof(path), app_job_service_path());
                app_job_stream_start_result_t status = app_job_stream_start(path);
                if(status == AppJobStreamStart_Ok) {
                    app_job_service_set_running(true);
                    last_job_progress_line = 0;
                    send_command_ack(frame.seq, frame.payload[0]);
                    send_job_progress_event(0, (uint32_t)app_job_service_total_lines());
                } else {
                    const char *reason = app_job_stream_start_reason(status);
                    send_error(frame.seq, ERROR_INVALID_STATE, reason);
                    send_job_error_event(reason);
                }
            }
            break;

        case PCNC_CMD_RESUME:
            if(!app_job_service_is_running())
                send_error(frame.seq, ERROR_NO_JOB_LOADED, "No running job");
            else if(app_job_stream_resume())
                send_command_ack(frame.seq, frame.payload[0]);
            else
                send_error(frame.seq, ERROR_INVALID_STATE, "Could not resume job");
            break;

        case PCNC_CMD_PAUSE:
            if(!app_job_service_is_running())
                send_error(frame.seq, ERROR_NO_JOB_LOADED, "No running job");
            else if(app_job_stream_pause())
                send_command_ack(frame.seq, frame.payload[0]);
            else
                send_error(frame.seq, ERROR_INVALID_STATE, "Could not pause job");
            break;

        case PCNC_CMD_ABORT:
            if(!app_job_service_is_running())
                send_error(frame.seq, ERROR_NO_JOB_LOADED, "No running job");
            else if(app_job_stream_abort()) {
                app_job_service_set_running(false);
                last_job_progress_line = 0;
                send_command_ack(frame.seq, frame.payload[0]);
            } else {
                send_error(frame.seq, ERROR_INVALID_STATE, "Could not stop job");
            }
            break;

        case PCNC_CMD_RESET:
            if(grbl.enqueue_realtime_command != nullptr) {
                send_command_ack(frame.seq, frame.payload[0]);
                task_add_delayed(soft_reset_task, nullptr, 25);
            } else {
                send_error(frame.seq, ERROR_INVALID_STATE, "Could not queue soft reset");
            }
            break;

        case PCNC_CMD_UNLOCK: {
            char line[] = "$X";
            status_code_t status = app_system_execute_line(line);
            if(status == Status_OK)
                send_command_ack(frame.seq, frame.payload[0]);
            else {
                const char *description = errors_get_description(status);
                send_error(frame.seq, ERROR_INVALID_STATE,
                           description == nullptr ? "Alarm unlock failed" : description);
            }
            break;
        }

        default:
            send_error(frame.seq, ERROR_UNKNOWN, "Command not implemented on Teensy yet");
            break;
    }
}

void app_protocol_start(void)
{
    task_add_delayed(app_protocol_poll_task, nullptr, 10);
    task_add_delayed(app_protocol_push_task, nullptr, 500);
}

void app_protocol_poll_task(void *data)
{
    (void)data;

    while(transport.poll(rx_frame)) {
        if(app_file_protocol_handle_command(transport, rx_frame))
            continue;

        if(rx_frame.type == FRAME_CMD)
            handle_command_frame(rx_frame);
    }

    task_add_delayed(app_protocol_poll_task, nullptr, 10);
}

void app_protocol_push_task(void *data)
{
    (void)data;

    if(transport.connected()) {
        machine_status_snapshot_t snapshot;
        machine_status_snapshot(&snapshot);

        EventPos pos = {};
        pos.message_type = EVENT_POS;
        pos.mx = snapshot.mpos_x;
        pos.my = snapshot.mpos_y;
        pos.mz = snapshot.mpos_z;
        pos.wx = snapshot.mpos_x;
        pos.wy = snapshot.mpos_y;
        pos.wz = snapshot.mpos_z;
        send_event(&pos, sizeof(pos));

        EventState state = {};
        state.message_type = EVENT_STATE;
        state.state = (uint8_t)protocol_state_from_mode(snapshot.mode);
        send_event(&state, sizeof(state));

        EventTemperature temperature = {};
        float temperature_c = 0.0f;
        temperature.message_type = EVENT_TEMPERATURE;
        temperature.has_temperature = app_bmp280_sensor_temperature_c(&temperature_c) ? 1 : 0;
        temperature.temperature_c = temperature_c;
        send_event(&temperature, sizeof(temperature));

        const char *bmp280_status_message = nullptr;
        uint8_t bmp280_status_level = 0;
        if(app_bmp280_sensor_status(&bmp280_status_message, &bmp280_status_level) &&
           bmp280_status_message != last_bmp280_status_message) {
            last_bmp280_status_message = bmp280_status_message;
            send_message_event(bmp280_status_level, bmp280_status_message);
        }

        if(hal.limits.get_state != nullptr) {
            const limit_signals_t limits = hal.limits.get_state();
            EventLimit limit = {};
            limit.message_type = EVENT_LIMIT;
            if(limits.min.x || limits.min2.x)
                limit.min_axes_mask |= AXES_X;
            if(limits.min.y || limits.min2.y)
                limit.min_axes_mask |= AXES_Y;
            if(limits.min.z || limits.min2.z)
                limit.min_axes_mask |= AXES_Z;
            if(limits.max.x || limits.max2.x)
                limit.max_axes_mask |= AXES_X;
            if(limits.max.y || limits.max2.y)
                limit.max_axes_mask |= AXES_Y;
            if(limits.max.z || limits.max2.z)
                limit.max_axes_mask |= AXES_Z;
            limit.axes_mask = (uint8_t)(limit.min_axes_mask | limit.max_axes_mask);
            if(limits.min.x || limits.max.x || limits.min2.x || limits.max2.x)
                limit.axes_mask |= AXES_X;
            if(limits.min.y || limits.max.y || limits.min2.y || limits.max2.y)
                limit.axes_mask |= AXES_Y;
            if(limits.min.z || limits.max.z || limits.min2.z || limits.max2.z)
                limit.axes_mask |= AXES_Z;
            send_event(&limit, sizeof(limit));
        }

        if(app_job_service_is_running()) {
            uint32_t line = 0;
            if(app_job_stream_current_line(&line)) {
                if(line != last_job_progress_line) {
                    last_job_progress_line = line;
                    send_job_progress_event(line, (uint32_t)app_job_service_total_lines());
                }
            } else {
                app_job_service_set_running(false);
                last_job_progress_line = 0;
                send_job_progress_event((uint32_t)app_job_service_total_lines(),
                                        (uint32_t)app_job_service_total_lines());
                send_job_complete_event();
            }
        }
    }

    task_add_delayed(app_protocol_push_task, nullptr, 500);
}
