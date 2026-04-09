#include "web/web_server.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

extern "C" {
#include "bsp/board_api.h"
#include "dhserver.h"
#include "dnserver.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "tusb.h"
}

#include "web/web_assets.h"

extern "C" {
uint8_t tud_network_mac_address[6] = {0x02, 0x50, 0x43, 0x4E, 0x43, 0x10};
}

namespace {

#define INIT_IP4(a, b, c, d) \
    { PP_HTONL(LWIP_MAKEU32(a, b, c, d)) }

constexpr std::size_t kMaxRequestBytes = 4096;
constexpr std::size_t kMaxJsonBytes = 4096;
constexpr std::size_t kMaxSseClients = 4;
constexpr uint16_t kHttpPort = 80;
constexpr uint32_t kStatusSampleIntervalMs = 125;
constexpr uint32_t kHeartbeatIntervalMs = 2000;

struct netif g_netif_data;
struct pbuf* g_received_frame = nullptr;
tcp_pcb* g_listen_pcb = nullptr;
WebServer* g_server = nullptr;

static const ip4_addr_t kIpAddr = INIT_IP4(192, 168, 7, 1);
static const ip4_addr_t kNetmask = INIT_IP4(255, 255, 255, 0);
static const ip4_addr_t kGateway = INIT_IP4(0, 0, 0, 0);

dhcp_entry_t g_entries[] = {
    {{0}, INIT_IP4(192, 168, 7, 2), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 3), 24 * 60 * 60},
    {{0}, INIT_IP4(192, 168, 7, 4), 24 * 60 * 60},
};

const dhcp_config_t kDhcpConfig = {
    .router = INIT_IP4(0, 0, 0, 0),
    .port = 67,
    .dns = INIT_IP4(192, 168, 7, 1),
    "portablecnc.usb",
    TU_ARRAY_SIZE(g_entries),
    g_entries,
};

struct HttpConnection {
    char request[kMaxRequestBytes + 1]{};
    std::size_t length = 0;
};

struct SseClient {
    tcp_pcb* pcb = nullptr;
};

SseClient g_sse_clients[kMaxSseClients];
char g_last_status_json[kMaxJsonBytes]{};
std::size_t g_last_status_size = 0;
uint32_t g_last_status_sample_at = 0;
uint32_t g_last_status_broadcast_at = 0;

class JsonBuilder {
public:
    JsonBuilder(char* buffer, std::size_t capacity) : buffer_(buffer), capacity_(capacity) {
        if (capacity_ > 0) {
            buffer_[0] = '\0';
        }
    }

    bool ok() const { return ok_; }
    std::size_t size() const { return length_; }

    void append(const char* text) {
        if (text == nullptr) {
            return;
        }
        append(text, std::strlen(text));
    }

    void append(const char* text, std::size_t size) {
        if (!ok_ || text == nullptr) {
            return;
        }

        if ((length_ + size + 1) > capacity_) {
            ok_ = false;
            return;
        }

        std::memcpy(buffer_ + length_, text, size);
        length_ += size;
        buffer_[length_] = '\0';
    }

    void append_char(char ch) { append(&ch, 1); }
    void append_bool(bool value) { append(value ? "true" : "false"); }

    void append_int(int value) {
        char text[24];
        std::snprintf(text, sizeof(text), "%d", value);
        append(text);
    }

    void append_float(float value) {
        char text[32];
        std::snprintf(text, sizeof(text), "%.1f", value);
        append(text);
    }

    void append_json_string(const char* text) {
        append_char('"');
        if (text != nullptr) {
            while (*text != '\0' && ok_) {
                switch (*text) {
                    case '\\':
                        append("\\\\");
                        break;
                    case '"':
                        append("\\\"");
                        break;
                    case '\n':
                        append("\\n");
                        break;
                    case '\r':
                        append("\\r");
                        break;
                    case '\t':
                        append("\\t");
                        break;
                    default:
                        append_char(*text);
                        break;
                }
                ++text;
            }
        }
        append_char('"');
    }

private:
    char* buffer_;
    std::size_t capacity_;
    std::size_t length_ = 0;
    bool ok_ = true;
};

const char* machine_state_text(MachineState state) {
    switch (state) {
        case MachineState::Booting:
            return "BOOT";
        case MachineState::Calibrating:
            return "CAL";
        case MachineState::Idle:
            return "IDLE";
        case MachineState::Running:
            return "RUN";
        case MachineState::Hold:
            return "HOLD";
        case MachineState::Alarm:
            return "ALARM";
        case MachineState::Estop:
            return "ESTOP";
    }
    return "--";
}

const char* storage_state_name(StorageState state) {
    switch (state) {
        case StorageState::Uninitialized:
            return "uninitialized";
        case StorageState::Mounting:
            return "mounting";
        case StorageState::Mounted:
            return "mounted";
        case StorageState::MountError:
            return "mount_error";
        case StorageState::ScanError:
            return "scan_error";
    }
    return "unknown";
}

const char* primary_action_name(PrimaryAction action) {
    switch (action) {
        case PrimaryAction::None:
            return "none";
        case PrimaryAction::LoadJob:
            return "load-job";
        case PrimaryAction::Start:
            return "start";
        case PrimaryAction::Pause:
            return "pause";
        case PrimaryAction::Resume:
            return "resume";
    }
    return "none";
}

bool extract_json_string(const char* body, const char* key, char* out, std::size_t out_size) {
    if (body == nullptr || key == nullptr || out == nullptr || out_size == 0) {
        return false;
    }

    char pattern[48];
    std::snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char* key_pos = std::strstr(body, pattern);
    if (key_pos == nullptr) {
        return false;
    }

    const char* colon = std::strchr(key_pos + std::strlen(pattern), ':');
    if (colon == nullptr) {
        return false;
    }

    const char* start = std::strchr(colon, '"');
    if (start == nullptr) {
        return false;
    }
    ++start;

    const char* end = std::strchr(start, '"');
    if (end == nullptr) {
        return false;
    }

    const std::size_t length = static_cast<std::size_t>(end - start);
    if (length >= out_size) {
        return false;
    }

    std::memcpy(out, start, length);
    out[length] = '\0';
    return true;
}

bool extract_json_index(const char* body, int& value) {
    if (body == nullptr) {
        return false;
    }

    const char* key_pos = std::strstr(body, "\"index\"");
    if (key_pos == nullptr) {
        return false;
    }

    const char* colon = std::strchr(key_pos, ':');
    if (colon == nullptr) {
        return false;
    }

    char* end = nullptr;
    const long parsed = std::strtol(colon + 1, &end, 10);
    if (end == (colon + 1)) {
        return false;
    }

    value = static_cast<int>(parsed);
    return true;
}

const char* find_header_end(const char* request) {
    return request == nullptr ? nullptr : std::strstr(request, "\r\n\r\n");
}

std::size_t extract_content_length(const char* request) {
    if (request == nullptr) {
        return 0;
    }

    const char* header = std::strstr(request, "Content-Length:");
    if (header == nullptr) {
        return 0;
    }

    header += std::strlen("Content-Length:");
    return static_cast<std::size_t>(std::strtoul(header, nullptr, 10));
}

bool parse_request_line(const char* request, char* method, std::size_t method_size, char* path, std::size_t path_size) {
    if (request == nullptr || method == nullptr || path == nullptr) {
        return false;
    }

    const char* first_space = std::strchr(request, ' ');
    const char* second_space = first_space == nullptr ? nullptr : std::strchr(first_space + 1, ' ');
    if (first_space == nullptr || second_space == nullptr) {
        return false;
    }

    const std::size_t method_length = static_cast<std::size_t>(first_space - request);
    const std::size_t path_length = static_cast<std::size_t>(second_space - (first_space + 1));
    if (method_length >= method_size || path_length >= path_size) {
        return false;
    }

    std::memcpy(method, request, method_length);
    method[method_length] = '\0';
    std::memcpy(path, first_space + 1, path_length);
    path[path_length] = '\0';
    return true;
}

bool request_complete(const HttpConnection* connection) {
    if (connection == nullptr) {
        return false;
    }

    const char* header_end = find_header_end(connection->request);
    if (header_end == nullptr) {
        return false;
    }

    if (std::strncmp(connection->request, "POST ", 5) != 0) {
        return true;
    }

    const std::size_t content_length = extract_content_length(connection->request);
    const std::size_t header_size = static_cast<std::size_t>((header_end - connection->request) + 4);
    return connection->length >= (header_size + content_length);
}

err_t linkoutput_fn(struct netif* netif, struct pbuf* p) {
    (void)netif;

    for (;;) {
        if (!tud_ready()) {
            return ERR_USE;
        }
        if (tud_network_can_xmit(p->tot_len)) {
            tud_network_xmit(p, 0);
            return ERR_OK;
        }
        tud_task();
    }
}

err_t ip4_output_fn(struct netif* netif, struct pbuf* p, const ip4_addr_t* addr) {
    return etharp_output(netif, p, addr);
}

err_t netif_init_cb(struct netif* netif) {
    LWIP_ASSERT("netif != NULL", (netif != nullptr));
    netif->mtu = CFG_TUD_NET_MTU;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP | NETIF_FLAG_UP;
    netif->state = nullptr;
    netif->name[0] = 'P';
    netif->name[1] = 'C';
    netif->linkoutput = linkoutput_fn;
    netif->output = ip4_output_fn;
    return ERR_OK;
}

void init_lwip() {
    lwip_init();
    g_netif_data.hwaddr_len = sizeof(tud_network_mac_address);
    std::memcpy(g_netif_data.hwaddr, tud_network_mac_address, sizeof(tud_network_mac_address));
    g_netif_data.hwaddr[5] ^= 0x01;
    struct netif* netif = netif_add(&g_netif_data, &kIpAddr, &kNetmask, &kGateway, nullptr, netif_init_cb, ip_input);
    netif_set_default(netif);
}

bool dns_query_proc(const char* name, ip4_addr_t* addr) {
    if (name == nullptr || addr == nullptr) {
        return false;
    }
    if (std::strcmp(name, "portablecnc.usb") == 0 || std::strcmp(name, "usb") == 0) {
        *addr = kIpAddr;
        return true;
    }
    return false;
}

err_t send_bytes(tcp_pcb* pcb, const char* data, std::size_t size) {
    while (size > 0) {
        const std::size_t chunk = size > 1024 ? 1024 : size;
        const err_t result = tcp_write(pcb, data, static_cast<u16_t>(chunk), TCP_WRITE_FLAG_COPY);
        if (result != ERR_OK) {
            return result;
        }
        data += chunk;
        size -= chunk;
    }
    return tcp_output(pcb);
}

void append_files_json(JsonBuilder& json, PortableCncController& controller) {
    json.append("\"selectedIndex\":");
    json.append_int(controller.jobs().selected_index());
    json.append(",\"canRunSelectedFile\":");
    json.append_bool(controller.can_run_selected_file());
    json.append(",\"files\":[");

    for (std::size_t i = 0; i < controller.jobs().count(); ++i) {
        const FileEntry& entry = controller.jobs().entry(i);
        if (i != 0) {
            json.append_char(',');
        }
        json.append("{\"name\":");
        json.append_json_string(entry.name);
        json.append(",\"summary\":");
        json.append_json_string(entry.summary);
        json.append(",\"sizeText\":");
        json.append_json_string(entry.size_text);
        json.append(",\"toolText\":");
        json.append_json_string(entry.tool_text);
        json.append(",\"zeroText\":");
        json.append_json_string(entry.zero_text);
        json.append(",\"selected\":");
        json.append_bool(controller.jobs().selected_index() == static_cast<int16_t>(i));
        json.append_char('}');
    }
    json.append_char(']');
}

bool build_status_json(PortableCncController& controller, StatusProvider& status_provider, char* buffer, std::size_t capacity) {
    JsonBuilder json(buffer, capacity);
    const StatusSnapshot snapshot = status_provider.current();

    json.append("{\"machineState\":");
    json.append_json_string(machine_state_text(controller.machine_state()));
    json.append(",\"storageState\":");
    json.append_json_string(storage_state_name(controller.storage_state()));
    json.append(",\"storageText\":");
    json.append_json_string(controller.storage_status_text());
    json.append(",\"primaryAction\":");
    json.append_json_string(primary_action_name(controller.primary_action()));
    json.append(",\"primaryLabel\":");
    json.append_json_string(controller.primary_action_label());
    json.append(",\"canJog\":");
    json.append_bool(controller.can_jog());
    json.append(",\"canSelectFile\":");
    json.append_bool(controller.can_select_file());
    json.append(",\"jog\":{");
    json.append("\"x\":");
    json.append_float(controller.jog().x());
    json.append(",\"y\":");
    json.append_float(controller.jog().y());
    json.append(",\"z\":");
    json.append_float(controller.jog().z());
    json.append(",\"stepIndex\":");
    json.append_int(controller.jog().step_index());
    json.append(",\"stepLabel\":");
    json.append_json_string(controller.jog().step_label());
    json.append(",\"feedIndex\":");
    json.append_int(controller.jog().feed_index());
    json.append(",\"feedLabel\":");
    json.append_json_string(controller.jog().feed_label());
    json.append(",\"feedRate\":");
    json.append_int(controller.jog().feed_rate_mm_min());
    json.append("},\"status\":{");
    json.append("\"machine\":");
    json.append_json_string(snapshot.machine);
    json.append(",\"sd\":");
    json.append_json_string(snapshot.sd);
    json.append(",\"usb\":");
    json.append_json_string(tud_ready() ? "NCM" : "--");
    json.append(",\"tool\":");
    json.append_json_string(snapshot.tool);
    json.append(",\"xyz\":");
    json.append_json_string(snapshot.xyz);
    json.append(",\"time\":");
    json.append_json_string(snapshot.time_text);
    json.append("},");
    append_files_json(json, controller);
    json.append_char('}');
    return json.ok();
}

bool build_files_json(PortableCncController& controller, char* buffer, std::size_t capacity) {
    JsonBuilder json(buffer, capacity);
    json.append_char('{');
    append_files_json(json, controller);
    json.append_char('}');
    return json.ok();
}

bool build_error_json(const char* error, char* buffer, std::size_t capacity) {
    JsonBuilder json(buffer, capacity);
    json.append("{\"error\":");
    json.append_json_string(error);
    json.append_char('}');
    return json.ok();
}

bool has_sse_clients() {
    for (const SseClient& client : g_sse_clients) {
        if (client.pcb != nullptr) {
            return true;
        }
    }
    return false;
}

void clear_sse_client(SseClient* client) {
    if (client == nullptr || client->pcb == nullptr) {
        return;
    }

    tcp_arg(client->pcb, nullptr);
    tcp_recv(client->pcb, nullptr);
    tcp_err(client->pcb, nullptr);
    tcp_poll(client->pcb, nullptr, 0);
    client->pcb = nullptr;
}

SseClient* allocate_sse_client() {
    for (SseClient& client : g_sse_clients) {
        if (client.pcb == nullptr) {
            return &client;
        }
    }
    return nullptr;
}

bool cache_status_json(char* payload, std::size_t& payload_size) {
    if (!build_status_json(g_server->controller(), g_server->status_provider(), payload, kMaxJsonBytes)) {
        return false;
    }

    payload_size = std::strlen(payload);
    return true;
}

bool send_sse_status(SseClient& client, const char* payload, std::size_t payload_size) {
    if (client.pcb == nullptr || payload == nullptr) {
        return false;
    }

    char message[kMaxJsonBytes + 32];
    const int prefix_length = std::snprintf(message, sizeof(message), "event: status\ndata: ");
    if (prefix_length <= 0) {
        return false;
    }

    const std::size_t prefix_size = static_cast<std::size_t>(prefix_length);
    if ((prefix_size + payload_size + 2) >= sizeof(message)) {
        return false;
    }

    std::memcpy(message + prefix_size, payload, payload_size);
    message[prefix_size + payload_size] = '\n';
    message[prefix_size + payload_size + 1] = '\n';
    return send_bytes(client.pcb, message, prefix_size + payload_size + 2) == ERR_OK;
}

bool send_sse_keepalive(SseClient& client) {
    static constexpr char kKeepalive[] = ": keepalive\n\n";
    return client.pcb != nullptr && send_bytes(client.pcb, kKeepalive, sizeof(kKeepalive) - 1) == ERR_OK;
}

void broadcast_status_if_needed() {
    if (!has_sse_clients()) {
        return;
    }

    const uint32_t now = board_millis();
    const bool sample_due = (now - g_last_status_sample_at) >= kStatusSampleIntervalMs;
    const bool heartbeat_due = (now - g_last_status_broadcast_at) >= kHeartbeatIntervalMs;
    if (!sample_due && !heartbeat_due) {
        return;
    }

    g_last_status_sample_at = now;

    char payload[kMaxJsonBytes];
    std::size_t payload_size = 0;
    if (!cache_status_json(payload, payload_size)) {
        return;
    }

    const bool changed =
        (payload_size != g_last_status_size) ||
        (std::memcmp(payload, g_last_status_json, payload_size) != 0);

    if (changed) {
        std::memcpy(g_last_status_json, payload, payload_size);
        g_last_status_json[payload_size] = '\0';
        g_last_status_size = payload_size;

        for (SseClient& client : g_sse_clients) {
            if (client.pcb != nullptr && !send_sse_status(client, payload, payload_size)) {
                tcp_pcb* failed_pcb = client.pcb;
                clear_sse_client(&client);
                tcp_abort(failed_pcb);
            }
        }

        g_last_status_broadcast_at = now;
        return;
    }

    if (!heartbeat_due) {
        return;
    }

    for (SseClient& client : g_sse_clients) {
        if (client.pcb != nullptr && !send_sse_keepalive(client)) {
            tcp_pcb* failed_pcb = client.pcb;
            clear_sse_client(&client);
            tcp_abort(failed_pcb);
        }
    }

    g_last_status_broadcast_at = now;
}

err_t close_connection(tcp_pcb* pcb, HttpConnection* connection) {
    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_err(pcb, nullptr);

    delete connection;

    const err_t close_result = tcp_close(pcb);
    if (close_result != ERR_OK) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    return ERR_OK;
}

err_t send_response(tcp_pcb* pcb, HttpConnection* connection, const char* status, const char* content_type, const char* body, std::size_t body_size) {
    char headers[256];
    const int header_length = std::snprintf(headers,
                                            sizeof(headers),
                                            "HTTP/1.1 %s\r\n"
                                            "Content-Type: %s\r\n"
                                            "Cache-Control: no-store\r\n"
                                            "Content-Length: %u\r\n"
                                            "Connection: close\r\n"
                                            "\r\n",
                                            status,
                                            content_type,
                                            static_cast<unsigned>(body_size));
    if (header_length <= 0) {
        tcp_abort(pcb);
        delete connection;
        return ERR_ABRT;
    }

    if (send_bytes(pcb, headers, static_cast<std::size_t>(header_length)) != ERR_OK ||
        send_bytes(pcb, body, body_size) != ERR_OK) {
        tcp_abort(pcb);
        delete connection;
        return ERR_ABRT;
    }

    return close_connection(pcb, connection);
}

err_t send_text_response(tcp_pcb* pcb, HttpConnection* connection, const char* status, const char* body) {
    return send_response(pcb, connection, status, "text/plain; charset=utf-8", body, std::strlen(body));
}

err_t sse_recv_cb(void* arg, tcp_pcb* pcb, pbuf* p, err_t err) {
    SseClient* client = static_cast<SseClient*>(arg);
    if (err != ERR_OK) {
        if (p != nullptr) {
            pbuf_free(p);
        }
        clear_sse_client(client);
        return err;
    }

    if (p == nullptr) {
        clear_sse_client(client);
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

void sse_err_cb(void* arg, err_t err) {
    (void)err;
    clear_sse_client(static_cast<SseClient*>(arg));
}

err_t handle_sse_request(tcp_pcb* pcb, HttpConnection* connection) {
    SseClient* client = allocate_sse_client();
    if (client == nullptr) {
        return send_text_response(pcb, connection, "503 Service Unavailable", "too many event clients");
    }

    static constexpr char kHeaders[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "X-Accel-Buffering: no\r\n"
        "\r\n";

    if (send_bytes(pcb, kHeaders, sizeof(kHeaders) - 1) != ERR_OK) {
        tcp_abort(pcb);
        delete connection;
        return ERR_ABRT;
    }

    client->pcb = pcb;
    tcp_arg(pcb, client);
    tcp_recv(pcb, sse_recv_cb);
    tcp_err(pcb, sse_err_cb);
    tcp_poll(pcb, nullptr, 0);
    tcp_nagle_disable(pcb);
    delete connection;

    char payload[kMaxJsonBytes];
    std::size_t payload_size = 0;
    if (!cache_status_json(payload, payload_size) || !send_sse_status(*client, payload, payload_size)) {
        clear_sse_client(client);
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    std::memcpy(g_last_status_json, payload, payload_size);
    g_last_status_json[payload_size] = '\0';
    g_last_status_size = payload_size;
    g_last_status_sample_at = board_millis();
    g_last_status_broadcast_at = g_last_status_sample_at;
    return ERR_OK;
}

err_t handle_api_response(tcp_pcb* pcb, HttpConnection* connection, const char* method, const char* path, const char* body) {
    char payload[kMaxJsonBytes];

    if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/status") == 0) {
        if (!build_status_json(g_server->controller(), g_server->status_provider(), payload, sizeof(payload))) {
            build_error_json("status_overflow", payload, sizeof(payload));
            return send_response(pcb, connection, "500 Internal Server Error", "application/json; charset=utf-8", payload, std::strlen(payload));
        }
        return send_response(pcb, connection, "200 OK", "application/json; charset=utf-8", payload, std::strlen(payload));
    }

    if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/files") == 0) {
        if (!build_files_json(g_server->controller(), payload, sizeof(payload))) {
            build_error_json("files_overflow", payload, sizeof(payload));
            return send_response(pcb, connection, "500 Internal Server Error", "application/json; charset=utf-8", payload, std::strlen(payload));
        }
        return send_response(pcb, connection, "200 OK", "application/json; charset=utf-8", payload, std::strlen(payload));
    }

    if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/control") == 0) {
        char command[16];
        ControlCommand parsed_command = ControlCommand::Start;
        if (!extract_json_string(body, "command", command, sizeof(command))) {
            build_error_json("bad_request", payload, sizeof(payload));
            return send_response(pcb, connection, "400 Bad Request", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        if (std::strcmp(command, "start") == 0) {
            parsed_command = ControlCommand::Start;
        } else if (std::strcmp(command, "pause") == 0) {
            parsed_command = ControlCommand::Pause;
        } else if (std::strcmp(command, "resume") == 0) {
            parsed_command = ControlCommand::Resume;
        } else {
            build_error_json("bad_request", payload, sizeof(payload));
            return send_response(pcb, connection, "400 Bad Request", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        if (!g_server->controller().apply_control(parsed_command)) {
            build_error_json("invalid_state", payload, sizeof(payload));
            return send_response(pcb, connection, "409 Conflict", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        build_status_json(g_server->controller(), g_server->status_provider(), payload, sizeof(payload));
        return send_response(pcb, connection, "200 OK", "application/json; charset=utf-8", payload, std::strlen(payload));
    }

    if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/files/select") == 0) {
        int index = -1;
        if (!extract_json_index(body, index)) {
            build_error_json("bad_request", payload, sizeof(payload));
            return send_response(pcb, connection, "400 Bad Request", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        if (!g_server->controller().select_file(static_cast<int16_t>(index))) {
            build_error_json("invalid_state", payload, sizeof(payload));
            return send_response(pcb, connection, "409 Conflict", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        build_status_json(g_server->controller(), g_server->status_provider(), payload, sizeof(payload));
        return send_response(pcb, connection, "200 OK", "application/json; charset=utf-8", payload, std::strlen(payload));
    }

    if (std::strcmp(method, "POST") == 0 && std::strcmp(path, "/api/jog") == 0) {
        char action[24];
        JogAction jog_action = JogAction::MoveXPositive;
        if (!extract_json_string(body, "action", action, sizeof(action))) {
            build_error_json("bad_request", payload, sizeof(payload));
            return send_response(pcb, connection, "400 Bad Request", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        if (std::strcmp(action, "x-") == 0) {
            jog_action = JogAction::MoveXNegative;
        } else if (std::strcmp(action, "x+") == 0) {
            jog_action = JogAction::MoveXPositive;
        } else if (std::strcmp(action, "y-") == 0) {
            jog_action = JogAction::MoveYNegative;
        } else if (std::strcmp(action, "y+") == 0) {
            jog_action = JogAction::MoveYPositive;
        } else if (std::strcmp(action, "z-") == 0) {
            jog_action = JogAction::MoveZNegative;
        } else if (std::strcmp(action, "z+") == 0) {
            jog_action = JogAction::MoveZPositive;
        } else if (std::strcmp(action, "step-fine") == 0) {
            jog_action = JogAction::SelectStepFine;
        } else if (std::strcmp(action, "step-medium") == 0) {
            jog_action = JogAction::SelectStepMedium;
        } else if (std::strcmp(action, "step-coarse") == 0) {
            jog_action = JogAction::SelectStepCoarse;
        } else if (std::strcmp(action, "feed-slow") == 0) {
            jog_action = JogAction::SelectFeedSlow;
        } else if (std::strcmp(action, "feed-normal") == 0) {
            jog_action = JogAction::SelectFeedNormal;
        } else if (std::strcmp(action, "feed-fast") == 0) {
            jog_action = JogAction::SelectFeedFast;
        } else if (std::strcmp(action, "home-all") == 0) {
            jog_action = JogAction::HomeAll;
        } else if (std::strcmp(action, "zero-all") == 0) {
            jog_action = JogAction::ZeroAll;
        } else {
            build_error_json("bad_request", payload, sizeof(payload));
            return send_response(pcb, connection, "400 Bad Request", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        if (!g_server->controller().apply_jog_action(jog_action)) {
            build_error_json("invalid_state", payload, sizeof(payload));
            return send_response(pcb, connection, "409 Conflict", "application/json; charset=utf-8", payload, std::strlen(payload));
        }

        build_status_json(g_server->controller(), g_server->status_provider(), payload, sizeof(payload));
        return send_response(pcb, connection, "200 OK", "application/json; charset=utf-8", payload, std::strlen(payload));
    }

    build_error_json("not_found", payload, sizeof(payload));
    return send_response(pcb, connection, "404 Not Found", "application/json; charset=utf-8", payload, std::strlen(payload));
}

err_t http_recv_cb(void* arg, tcp_pcb* pcb, pbuf* p, err_t err) {
    HttpConnection* connection = static_cast<HttpConnection*>(arg);
    if (err != ERR_OK) {
        if (p != nullptr) {
            pbuf_free(p);
        }
        delete connection;
        return err;
    }

    if (p == nullptr) {
        delete connection;
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);

    if ((connection->length + p->tot_len) > kMaxRequestBytes) {
        pbuf_free(p);
        return send_text_response(pcb, connection, "413 Payload Too Large", "payload too large");
    }

    pbuf_copy_partial(p, connection->request + connection->length, p->tot_len, 0);
    connection->length += p->tot_len;
    connection->request[connection->length] = '\0';
    pbuf_free(p);

    const char* header_end = find_header_end(connection->request);
    if (header_end != nullptr) {
        const std::size_t header_size = static_cast<std::size_t>((header_end - connection->request) + 4);
        const std::size_t content_length = extract_content_length(connection->request);
        if ((header_size + content_length) > kMaxRequestBytes) {
            return send_text_response(pcb, connection, "413 Payload Too Large", "payload too large");
        }
    }

    if (!request_complete(connection)) {
        return ERR_OK;
    }

    char method[8];
    char path[128];
    if (!parse_request_line(connection->request, method, sizeof(method), path, sizeof(path))) {
        return send_text_response(pcb, connection, "400 Bad Request", "bad request");
    }

    if (std::strcmp(path, "/favicon.ico") == 0) {
        return send_response(pcb, connection, "204 No Content", "text/plain; charset=utf-8", "", 0);
    }

    if (std::strcmp(method, "GET") == 0 && std::strcmp(path, "/api/events") == 0) {
        return handle_sse_request(pcb, connection);
    }

    if (std::strncmp(path, "/api/", 5) == 0) {
        const char* body = "";
        if (const char* body_start = find_header_end(connection->request)) {
            body = body_start + 4;
        }
        return handle_api_response(pcb, connection, method, path, body);
    }

    if (std::strcmp(method, "GET") != 0) {
        return send_text_response(pcb, connection, "405 Method Not Allowed", "method not allowed");
    }

    const web_assets::Asset* asset = web_assets::find(path);
    if (asset == nullptr) {
        return send_text_response(pcb, connection, "404 Not Found", "not found");
    }

    return send_response(pcb, connection, "200 OK", asset->content_type, asset->data, asset->size);
}

void http_err_cb(void* arg, err_t err) {
    (void)err;
    delete static_cast<HttpConnection*>(arg);
}

err_t http_accept_cb(void* arg, tcp_pcb* pcb, err_t err) {
    (void)arg;
    if (err != ERR_OK) {
        return err;
    }

    HttpConnection* connection = new (std::nothrow) HttpConnection();
    if (connection == nullptr) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    tcp_arg(pcb, connection);
    tcp_recv(pcb, http_recv_cb);
    tcp_err(pcb, http_err_cb);
    return ERR_OK;
}

bool init_http_server() {
    if (g_listen_pcb != nullptr) {
        return true;
    }

    tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == nullptr) {
        return false;
    }

    if (tcp_bind(pcb, IP_ANY_TYPE, kHttpPort) != ERR_OK) {
        tcp_abort(pcb);
        return false;
    }

    g_listen_pcb = tcp_listen_with_backlog(pcb, 4);
    if (g_listen_pcb == nullptr) {
        return false;
    }

    tcp_accept(g_listen_pcb, http_accept_cb);
    return true;
}

void service_traffic() {
    if (g_received_frame != nullptr) {
        if (ethernet_input(g_received_frame, &g_netif_data) != ERR_OK) {
            pbuf_free(g_received_frame);
        }
        g_received_frame = nullptr;
        tud_network_recv_renew();
    }

    sys_check_timeouts();
}

}  // namespace

WebServer::WebServer(PortableCncController& controller, StatusProvider& status_provider)
    : controller_(controller), status_provider_(status_provider) {}

bool WebServer::init() {
    if (initialized_) {
        return true;
    }

    g_server = this;

    board_init();

    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    tusb_init(BOARD_TUD_RHPORT, &dev_init);

    init_lwip();
    while (!netif_is_up(&g_netif_data)) {}
    while (dhserv_init(&kDhcpConfig) != ERR_OK) {}
    while (dnserv_init(IP_ADDR_ANY, 53, dns_query_proc) != ERR_OK) {}
    if (!init_http_server()) {
        return false;
    }

    initialized_ = true;
    return true;
}

void WebServer::poll() {
    if (!initialized_) {
        return;
    }

    tud_task();
    service_traffic();
    broadcast_status_if_needed();
}

PortableCncController& WebServer::controller() {
    return controller_;
}

StatusProvider& WebServer::status_provider() {
    return status_provider_;
}

extern "C" bool tud_network_recv_cb(const uint8_t* src, uint16_t size) {
    if (g_received_frame != nullptr) {
        return false;
    }

    if (size == 0) {
        return true;
    }

    struct pbuf* p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (p == nullptr) {
        return false;
    }

    std::memcpy(p->payload, src, size);
    g_received_frame = p;
    return true;
}

extern "C" uint16_t tud_network_xmit_cb(uint8_t* dst, void* ref, uint16_t arg) {
    (void)arg;
    return pbuf_copy_partial(static_cast<struct pbuf*>(ref), dst, static_cast<struct pbuf*>(ref)->tot_len, 0);
}

extern "C" void tud_network_init_cb(void) {
    if (g_received_frame != nullptr) {
        pbuf_free(g_received_frame);
        g_received_frame = nullptr;
    }
}

extern "C" sys_prot_t sys_arch_protect(void) {
    return 0;
}

extern "C" void sys_arch_unprotect(sys_prot_t pval) {
    (void)pval;
}

extern "C" uint32_t sys_now(void) {
    return board_millis();
}
