#include "lan_scan.h"

#include <WiFi.h>

extern "C" {
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/priv/tcpip_priv.h"
}

static const uint8_t  SWEEP_PASSES   = 2;
static const uint8_t  BATCH_SIZE     = 8;
static const uint32_t BATCH_GAP_MS   = 120;
static const uint32_t SETTLE_MS      = 1500;  // wait for late replies after the last batch

static LanDevice devices[LAN_MAX_DEVICES];
static size_t    deviceCount = 0;

static bool     running    = false;
static bool     changed    = false;
static uint32_t firstHost  = 0;   // host byte order
static uint32_t hostCount  = 0;
static uint32_t cursor     = 0;   // index into hosts across all passes
static uint32_t nextBatchMs = 0;
static uint32_t sweepDoneMs = 0;
static uint32_t finishedMs = 0;

// --- lwIP calls, marshalled onto the tcpip thread ---------------------------
struct ArpCall {
  tcpip_api_call_data call;
  uint32_t            ips[BATCH_SIZE];  // network byte order
  uint8_t             count;
};

static struct netif *staNetif() {
  const uint32_t local = (uint32_t)WiFi.localIP();
  struct netif  *nif;
  NETIF_FOREACH(nif) {
    if (ip4_addr_get_u32(netif_ip4_addr(nif)) == local) return nif;
  }
  return nullptr;
}

static err_t arpRequestFn(tcpip_api_call_data *data) {
  ArpCall      *c   = reinterpret_cast<ArpCall *>(data);
  struct netif *nif = staNetif();
  if (!nif) return ERR_IF;
  for (uint8_t i = 0; i < c->count; i++) {
    ip4_addr_t addr;
    ip4_addr_set_u32(&addr, c->ips[i]);
    etharp_request(nif, &addr);
  }
  return ERR_OK;
}

struct HarvestCall {
  tcpip_api_call_data call;
  uint32_t            ips[ARP_TABLE_SIZE];
  uint8_t             macs[ARP_TABLE_SIZE][6];
  uint8_t             count;
};

static err_t harvestFn(tcpip_api_call_data *data) {
  HarvestCall *h = reinterpret_cast<HarvestCall *>(data);
  h->count       = 0;
  for (size_t i = 0; i < ARP_TABLE_SIZE; i++) {
    ip4_addr_t   *ip;
    struct netif *nif;
    struct eth_addr *mac;
    if (etharp_get_entry(i, &ip, &nif, &mac)) {
      h->ips[h->count] = ip4_addr_get_u32(ip);
      memcpy(h->macs[h->count], mac->addr, 6);
      h->count++;
    }
  }
  return ERR_OK;
}

// --- Device list ------------------------------------------------------------
static const char *classify(uint32_t ip, const uint8_t *mac) {
  if (ip == (uint32_t)WiFi.gatewayIP()) return "Gateway";
  if (mac[0] & 0x02) return "Private MAC";

  struct Oui { uint8_t b[3]; const char *name; };
  static const Oui ouis[] = {
      {{0x24, 0x0A, 0xC4}, "Espressif"}, {{0x30, 0xAE, 0xA4}, "Espressif"},
      {{0x3C, 0x71, 0xBF}, "Espressif"}, {{0x7C, 0x9E, 0xBD}, "Espressif"},
      {{0x84, 0xCC, 0xA8}, "Espressif"}, {{0xA4, 0xCF, 0x12}, "Espressif"},
      {{0xAC, 0x67, 0xB2}, "Espressif"}, {{0xC8, 0xC9, 0xA3}, "Espressif"},
      {{0xE8, 0xDB, 0x84}, "Espressif"}, {{0xEC, 0x94, 0xCB}, "Espressif"},
      {{0x08, 0x3A, 0xF2}, "Espressif"}, {{0x94, 0xB9, 0x7E}, "Espressif"},
      {{0xB8, 0x27, 0xEB}, "Raspberry Pi"}, {{0xDC, 0xA6, 0x32}, "Raspberry Pi"},
      {{0xE4, 0x5F, 0x01}, "Raspberry Pi"}, {{0x2C, 0xCF, 0x67}, "Raspberry Pi"},
  };
  for (const Oui &o : ouis) {
    if (memcmp(mac, o.b, 3) == 0) return o.name;
  }
  return "Device";
}

static void addDevice(uint32_t ip, const uint8_t *mac) {
  for (size_t i = 0; i < deviceCount; i++) {
    if (devices[i].ip == ip) {
      if (memcmp(devices[i].mac, mac, 6) != 0) {
        memcpy(devices[i].mac, mac, 6);
        devices[i].kind = classify(ip, mac);
        changed         = true;
      }
      return;
    }
  }
  if (deviceCount >= LAN_MAX_DEVICES) return;

  // Insert sorted by host-order IP.
  const uint32_t key = ntohl(ip);
  size_t pos = deviceCount;
  while (pos > 0 && ntohl(devices[pos - 1].ip) > key) {
    devices[pos] = devices[pos - 1];
    pos--;
  }
  devices[pos].ip = ip;
  memcpy(devices[pos].mac, mac, 6);
  devices[pos].kind = classify(ip, mac);
  deviceCount++;
  changed = true;
}

static void addSelf() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  const uint32_t ip = (uint32_t)WiFi.localIP();
  addDevice(ip, mac);
  for (size_t i = 0; i < deviceCount; i++) {
    if (devices[i].ip == ip) devices[i].kind = "This hub";
  }
}

static void harvest() {
  static HarvestCall h;
  if (tcpip_api_call(harvestFn, &h.call) != ERR_OK) return;
  for (uint8_t i = 0; i < h.count; i++) addDevice(h.ips[i], h.macs[i]);
}

// --- Public API -------------------------------------------------------------
void lanScanStart() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[LAN ] Scan skipped - not connected"));
    return;
  }
  const uint32_t ip   = ntohl((uint32_t)WiFi.localIP());
  uint32_t       mask = ntohl((uint32_t)WiFi.subnetMask());
  if (mask < 0xFFFFFF00UL) mask = 0xFFFFFF00UL;  // cap the sweep at a /24

  firstHost = (ip & mask) + 1;
  hostCount = (~mask) - 1;  // excludes network and broadcast addresses

  deviceCount = 0;
  addSelf();
  cursor      = 0;
  nextBatchMs = 0;
  sweepDoneMs = 0;
  running     = true;
  changed     = true;
  Serial.printf("[LAN ] Scanning %s/%d (%lu hosts)\n",
                IPAddress(htonl(ip & mask)).toString().c_str(),
                __builtin_popcount(mask), (unsigned long)hostCount);
}

void lanScanLoop(uint32_t now) {
  if (!running) return;
  if (WiFi.status() != WL_CONNECTED) {
    running = false;
    changed = true;
    Serial.println(F("[LAN ] Scan aborted - link lost"));
    return;
  }
  if ((int32_t)(now - nextBatchMs) < 0) return;
  nextBatchMs = now + BATCH_GAP_MS;

  const uint32_t total = hostCount * SWEEP_PASSES;
  if (cursor < total) {
    static ArpCall c;
    c.count = 0;
    while (c.count < BATCH_SIZE && cursor < total) {
      const uint32_t host = firstHost + (cursor % hostCount);
      c.ips[c.count++]    = htonl(host);
      cursor++;
    }
    tcpip_api_call(arpRequestFn, &c.call);
    harvest();
    if (cursor >= total) sweepDoneMs = now;
    changed = true;  // progress moved
    return;
  }

  harvest();
  if (now - sweepDoneMs >= SETTLE_MS) {
    running    = false;
    finishedMs = now ? now : 1;
    changed    = true;
    Serial.printf("[LAN ] Scan complete: %u device(s)\n", (unsigned)deviceCount);
    for (size_t i = 0; i < deviceCount; i++) {
      const LanDevice &d = devices[i];
      Serial.printf("[LAN ]   %-15s %02X:%02X:%02X:%02X:%02X:%02X  %s\n",
                    IPAddress(d.ip).toString().c_str(), d.mac[0], d.mac[1], d.mac[2],
                    d.mac[3], d.mac[4], d.mac[5], d.kind);
    }
  }
}

bool lanScanRunning() { return running; }

uint8_t lanScanProgress() {
  if (!running) return 100;
  const uint32_t total = hostCount * SWEEP_PASSES;
  return total ? (uint8_t)min<uint32_t>(99, cursor * 100 / total) : 99;
}

uint32_t lanScanFinishedMs() { return finishedMs; }
size_t   lanDeviceCount() { return deviceCount; }
const LanDevice &lanDevice(size_t index) { return devices[index]; }

bool lanScanChanged() {
  const bool c = changed;
  changed      = false;
  return c;
}
