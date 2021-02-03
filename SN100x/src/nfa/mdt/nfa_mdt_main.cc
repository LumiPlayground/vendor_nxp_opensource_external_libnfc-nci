/******************************************************************************
 *
 *  Copyright 2020 NXP
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 ******************************************************************************/
#if (NXP_EXTNS == TRUE)
#include <android-base/stringprintf.h>
#include <base/logging.h>
#include <nfa_mdt_int.h>
#include "NfcAdaptation.h"
#include "nfa_dm_int.h"
#include "nfa_hci_defs.h"
#include "nfc_api.h"
using android::base::StringPrintf;

extern bool nfc_debug_enabled;
ThreadCondVar mdtDeactivateEvtCb;
ThreadCondVar mdtDiscoverymapEvt;
tNFA_HCI_EVT_DATA p_evtdata;

/*Local static function declarations*/
void nfa_mdt_process_hci_evt(tNFA_HCI_EVT event, tNFA_HCI_EVT_DATA* p_data);
void nfa_mdt_deactivate_req_evt(tNFC_DISCOVER_EVT event,
                                tNFC_DISCOVER* evt_data);
void nfa_mdt_discovermap_cb(tNFC_DISCOVER_EVT event, tNFC_DISCOVER* p_data);

/******************************************************************************
 **  Constants
 *****************************************************************************/
#define NFC_NUM_INTERFACE_MAP 3
#define NFC_SWP_RD_NUM_INTERFACE_MAP 1

/*******************************************************************************
**
** Function         nfa_mdt_start
**
** Description      Process to send discover map command with RF interface.
**
** Returns          None
**
*******************************************************************************/
void* nfa_mdt_start(void*) {
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(" nfa_mdt_start: Enter");
  tNFA_STATUS status = NFA_STATUS_FAILED;
  tNCI_DISCOVER_MAPS* p_intf_mapping = nullptr;

  const tNCI_DISCOVER_MAPS
      nfc_interface_mapping_mfc[NFC_SWP_RD_NUM_INTERFACE_MAP] = {
          {NCI_PROTOCOL_T2T, NCI_INTERFACE_MODE_POLL, NCI_INTERFACE_FRAME}};
  p_intf_mapping = (tNCI_DISCOVER_MAPS*)nfc_interface_mapping_mfc;

  if (p_intf_mapping != nullptr &&
      (nfa_dm_cb.disc_cb.disc_state == NFA_DM_RFST_IDLE ||
       nfa_dm_cb.disc_cb.disc_state == NFA_DM_RFST_POLL_ACTIVE)) {
    mdtDiscoverymapEvt.lock();
    (void)NFC_DiscoveryMap(NFC_SWP_RD_NUM_INTERFACE_MAP, p_intf_mapping,
                           nfa_mdt_discovermap_cb);
    mdtDiscoverymapEvt.wait();
    status = mdt_t.rsp_status;
  }
  if (status == NFC_STATUS_OK) {
    mdt_t.mdt_state = ENABLE;
  }
  /* notify NFA_HCI_EVENT_RCVD_EVT to the application */
  nfa_hciu_send_to_apps_handling_connectivity_evts(NFA_HCI_EVENT_RCVD_EVT,
                                                   &p_evtdata);

  pthread_exit(NULL);
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(" nfa_mdt_start:Exit");
}

/*******************************************************************************
**
** Function         nfa_mdt_stop
**
** Description      Process to reset the default settings for discover map
**                  command.
**
** Returns          None
**
*******************************************************************************/
void* nfa_mdt_stop(void*) {
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(" nfa_mdt_stop:Enter");
  tNCI_DISCOVER_MAPS* p_intf_mapping = nullptr;

  const tNCI_DISCOVER_MAPS
      nfc_interface_mapping_default[NFC_NUM_INTERFACE_MAP] = {
        /* Protocols that use Frame Interface do not need to be included in
           the interface mapping */
        {NCI_PROTOCOL_ISO_DEP, NCI_INTERFACE_MODE_POLL_N_LISTEN,
         NCI_INTERFACE_ISO_DEP}
#if (NFC_RW_ONLY == FALSE)
        , /* this can not be set here due to 2079xB0 NFCC issues */
        {NCI_PROTOCOL_NFC_DEP, NCI_INTERFACE_MODE_POLL_N_LISTEN,
         NCI_INTERFACE_NFC_DEP}
#endif
        , /* This mapping is for Felica on DH  */
        {NCI_PROTOCOL_T3T, NCI_INTERFACE_MODE_LISTEN, NCI_INTERFACE_FRAME},
      };

  p_intf_mapping = (tNCI_DISCOVER_MAPS*)nfc_interface_mapping_default;
  mdtDeactivateEvtCb.lock();
  if(NFA_STATUS_OK == NFA_StopRfDiscovery()) {
    if (nfa_dm_cb.disc_cb.disc_state == NFA_DM_RFST_POLL_ACTIVE) {
      mdt_t.wait_for_deact_ntf = true;
    }
    mdtDeactivateEvtCb.wait();
    if (p_intf_mapping != nullptr && mdt_t.rsp_status == NFA_STATUS_OK &&
        (nfa_dm_cb.disc_cb.disc_state == NFA_DM_RFST_IDLE ||
         nfa_dm_cb.disc_cb.disc_state == NFA_DM_RFST_POLL_ACTIVE)) {
      mdtDiscoverymapEvt.lock();
      (void)NFC_DiscoveryMap(NFC_NUM_INTERFACE_MAP, p_intf_mapping,
                             nfa_mdt_discovermap_cb);
      mdtDiscoverymapEvt.wait();
    }
  }
  nfa_hciu_send_to_apps_handling_connectivity_evts(NFA_HCI_EVENT_RCVD_EVT,
                                                   &p_evtdata);
  mdt_t.mdt_state = DISABLE;
  pthread_exit(NULL);
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf(" nfa_mdt_stop:Exit");
}
/*******************************************************************************
 **
 ** Function:        nfa_mdt_discovermap_cb
 **
 ** Description:     callback for Discover Map command
 **
 ** Returns:         void
 **
 *******************************************************************************/
void nfa_mdt_discovermap_cb(tNFC_DISCOVER_EVT event, tNFC_DISCOVER* p_data) {
  switch (event) {
    case NFC_MAP_DEVT:
      if (p_data) mdt_t.rsp_status = p_data->status;
      mdtDiscoverymapEvt.signal();
      break;
  }
  return;
}
/*******************************************************************************
**
** Function         mdtCallback
**
** Description      callback for Mdt specific HCI events
**
** Returns          none
**
*******************************************************************************/
void nfa_mdt_process_hci_evt(tNFA_HCI_EVT event, tNFA_HCI_EVT_DATA* p_data) {
  pthread_t MdtThread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  p_evtdata = *p_data;
  switch (event) {
    case NFA_MDT_START_EVT:
      if (mdt_t.mdt_state == ENABLE) break;
      if (pthread_create(&MdtThread, &attr, nfa_mdt_start, nullptr) < 0) {
        DLOG_IF(ERROR, nfc_debug_enabled)
            << StringPrintf("MdtThread start creation failed");
      }
      break;
    case NFA_MDT_STOP_EVT:
      if (mdt_t.mdt_state == DISABLE) break;
      if (pthread_create(&MdtThread, &attr, nfa_mdt_stop, nullptr) < 0) {
        DLOG_IF(ERROR, nfc_debug_enabled)
            << StringPrintf("MdtThread stop creation failed");
      }
      break;
    default:
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s Unknown Event!!", __func__);
      break;
  }
  pthread_attr_destroy(&attr);
}
/*******************************************************************************
**
** Function         nfa_mdt_deactivate_req_evt
**
** Description      callback for Mdt deactivate response
**
** Returns          None
**
*******************************************************************************/
void nfa_mdt_deactivate_req_evt(tNFC_DISCOVER_EVT event,
                                tNFC_DISCOVER* evt_data) {
  DLOG_IF(INFO, nfc_debug_enabled)
      << StringPrintf("%s :Enter event:0x%2x", __func__, event);

  switch (event) {
    case NFA_DM_RF_DEACTIVATE_RSP:
      if (mdt_t.mdt_state == ENABLE && !mdt_t.wait_for_deact_ntf &&
          evt_data->deactivate.type == NFA_DEACTIVATE_TYPE_IDLE) {
        mdt_t.rsp_status = evt_data->deactivate.status;
        mdtDeactivateEvtCb.signal();
      }
      break;
    case NFA_DM_RF_DEACTIVATE_NTF:
      /*Handle the scenario where tag is present and stop req is trigger*/
      if (mdt_t.mdt_state == ENABLE && mdt_t.wait_for_deact_ntf &&
          (evt_data->deactivate.type == NFA_DEACTIVATE_TYPE_IDLE)) {
        mdt_t.rsp_status = evt_data->deactivate.status;
        mdtDeactivateEvtCb.signal();
        mdt_t.wait_for_deact_ntf = false;
      }
      break;
    default:
      DLOG_IF(INFO, nfc_debug_enabled)
          << StringPrintf("%s Unknown Event!!", __func__);
      break;
  }
  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s :Exit ", __func__);
}
/*******************************************************************************
**
** Function         nfa_mdt_check_hci_evt
**
** Description      This function shall be called to check whether MDT HCI EVT
**                   or not.
**
** Returns          TRUE/FALSE
**
*******************************************************************************/
bool nfa_mdt_check_hci_evt(tNFA_HCI_EVT_DATA* evt_data) {
  static const uint8_t TAG_MDT_AID = 0x81;
  static const uint8_t TAG_MDT_EVT_DATA = 0x82;
  static const uint8_t INDEX1_FIELD_VALUE = 0xA0;
  static const uint8_t INDEX2_FIELD_VALUE = 0xFE;
  uint8_t inst = evt_data->rcvd_evt.evt_code;
  uint8_t* p_data = evt_data->rcvd_evt.p_evt_buf;
  bool is_require = false;
  uint8_t aid[NFC_MAX_AID_LEN] = {0xA0, 0x00, 0x00, 0x03, 0x96, 0x54,
                                  0x53, 0x00, 0x00, 0x00, 0x01, 0x01,
                                  0x80, 0x00, 0x00, 0x01};
  uint8_t len_aid = 0;

  DLOG_IF(INFO, nfc_debug_enabled) << StringPrintf("%s :Enter ", __func__);
  if (inst == NFA_HCI_EVT_TRANSACTION && *p_data++ == TAG_MDT_AID) {
    len_aid = *p_data++;
    if (memcmp(p_data, aid, len_aid) == 0) {
      p_data += len_aid;
      if (*p_data == TAG_MDT_EVT_DATA) {
        if (*(p_data + 3) == INDEX1_FIELD_VALUE &&
            *(p_data + 4) == INDEX2_FIELD_VALUE) {
          if (*(p_data + 6) == 0x01) {
            nfa_mdt_process_hci_evt(NFA_MDT_START_EVT, evt_data);
            is_require = true;
          } else if (*(p_data + 6) == 0x00) {
            is_require = true;
            nfa_mdt_process_hci_evt(NFA_MDT_STOP_EVT, evt_data);
          }
        }
      }
    }
  }
  DLOG_IF(INFO, nfc_debug_enabled)
      << StringPrintf("%s :Exit %d", __func__, is_require);
  return is_require;
}
#endif
