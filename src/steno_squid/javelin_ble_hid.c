/* Javelin's existing WebHID console carried by an independent BLE HID service.
 * No report IDs: the unmodified website calls sendReport(0, ...).
 */
#include "javelin_hid_transport.h"

#include <errno.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zmk/ble.h>

static const uint8_t report_map[] = {
    0x06, 0x31, 0xff, 0x09, 0x74, 0xa1, 0x01,
    0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,
    0x95, JAVELIN_HID_REPORT_BYTES, 0x09, 0x75, 0x81, 0x02,
    0x95, JAVELIN_HID_REPORT_BYTES, 0x09, 0x76, 0x91, 0x02, 0xc0,
};
static const uint8_t hid_info[] = {0x11, 0x01, 0x00, 0x02};
static const uint8_t input_ref[] = {0, 1};
static const uint8_t output_ref[] = {0, 2};
static const uint8_t empty_report[JAVELIN_HID_REPORT_BYTES];
static struct k_spinlock peer_lock;
static struct bt_conn *peer;
static uint32_t generation = 1;

static bool allowed_peer(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.type == BT_CONN_TYPE_LE &&
           info.role == BT_CONN_ROLE_PERIPHERAL &&
           bt_conn_get_security(conn) >= BT_SECURITY_L2 &&
           bt_addr_le_cmp(zmk_ble_active_profile_addr(), bt_conn_get_dst(conn)) == 0;
}

static ssize_t read_map(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map, sizeof(report_map));
}

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                         void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, hid_info, sizeof(hid_info));
}

static ssize_t read_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                        void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, 2);
}

static ssize_t read_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           void *buf, uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, empty_report, sizeof(empty_report));
}

static ssize_t write_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                            const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset || (flags & BT_GATT_WRITE_FLAG_PREPARE)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (!len || len > JAVELIN_HID_REPORT_BYTES) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    if (!allowed_peer(conn)) {
        return BT_GATT_ERR(BT_ATT_ERR_AUTHORIZATION);
    }

    struct bt_conn *old = NULL;
    k_spinlock_key_t key = k_spin_lock(&peer_lock);
    if (peer != conn) {
        old = peer;
        peer = bt_conn_ref(conn);
        generation++;
    }
    const uint32_t session = generation;
    k_spin_unlock(&peer_lock, key);
    if (old) {
        bt_conn_unref(old);
    }
    if (steno_squid_javelin_ble_receive(buf, len, session) != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
    }
    return len;
}

static void subscription_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);
    ARG_UNUSED(value);
}

static ssize_t control_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset || len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    return len;
}

/* The input value is attribute 6; keep its index beside the declaration. */
enum { INPUT_VALUE = 6 };
BT_GATT_SERVICE_DEFINE(javelin_hog,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ, read_info, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ_ENCRYPT, read_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ_ENCRYPT, read_report, NULL, NULL),
    BT_GATT_CCC(subscription_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT,
        read_ref, NULL, input_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, read_report, write_report, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT,
        read_ref, NULL, output_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
        BT_GATT_PERM_WRITE_ENCRYPT, NULL, control_point, NULL));

static struct bt_conn *session_peer(uint32_t session) {
    k_spinlock_key_t key = k_spin_lock(&peer_lock);
    struct bt_conn *conn = peer && generation == session ? bt_conn_ref(peer) : NULL;
    k_spin_unlock(&peer_lock, key);
    return conn;
}

int steno_squid_javelin_ble_session_valid(uint32_t session) {
    struct bt_conn *conn = session_peer(session);
    if (!conn) {
        return 0;
    }
    const bool valid = allowed_peer(conn) &&
        bt_gatt_is_subscribed(conn, &javelin_hog.attrs[INPUT_VALUE], BT_GATT_CCC_NOTIFY);
    bt_conn_unref(conn);
    return valid;
}

int steno_squid_javelin_ble_send(const void *data, size_t size, uint32_t session) {
    if (size != JAVELIN_HID_REPORT_BYTES) {
        return -EINVAL;
    }
    struct bt_conn *conn = session_peer(session);
    if (!conn) {
        return -ENOTCONN;
    }
    int rc = -ENOTCONN;
    if (allowed_peer(conn) &&
        bt_gatt_is_subscribed(conn, &javelin_hog.attrs[INPUT_VALUE], BT_GATT_CCC_NOTIFY)) {
        if (bt_gatt_get_mtu(conn) < size + 3) {
            rc = -EMSGSIZE;
        } else {
            /* GATT copies the payload into its ATT buffer before returning.
             * This worker may block for TX buffers; the Bluetooth RX thread never does.
             */
            rc = bt_gatt_notify(conn, &javelin_hog.attrs[INPUT_VALUE], data, size);
        }
    }
    bt_conn_unref(conn);
    return rc;
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    struct bt_conn *old = NULL;
    k_spinlock_key_t key = k_spin_lock(&peer_lock);
    if (peer == conn) {
        old = peer;
        peer = NULL;
        generation++;
    }
    k_spin_unlock(&peer_lock, key);
    if (old) {
        bt_conn_unref(old);
    }
}

BT_CONN_CB_DEFINE(javelin_hog_callbacks) = {.disconnected = disconnected};
