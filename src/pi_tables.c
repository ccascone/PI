/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "PI/pi_tables.h"
#include "PI/target/pi_tables_imp.h"
#include "PI/int/pi_int.h"
#include "PI/int//serialize.h"

#include <stdlib.h>
#include <string.h>

void pi_entry_properties_clear(pi_entry_properties_t *properties) {
  memset(properties, 0, sizeof(*properties));
}

pi_status_t pi_entry_properties_set(pi_entry_properties_t *properties,
                                    pi_entry_property_type_t property_type,
                                    uint32_t property_value) {
                                    /* const pi_value_t *property_value) { */
  switch (property_type) {
    case PI_ENTRY_PROPERTY_TYPE_PRIORITY:
      properties->priority = property_value;
      break;
    case PI_ENTRY_PROPERTY_TYPE_TTL:
      properties->ttl = property_value;
      break;
    default:
      return PI_STATUS_INVALID_ENTRY_PROPERTY;
  }
  assert(property_type <= 8 * sizeof(properties->valid_properties));
  properties->valid_properties |= (1 << property_type);
  // TODO(antonin): return different code if the property was set previously
  return PI_STATUS_SUCCESS;
}

bool pi_entry_properties_is_set(const pi_entry_properties_t *properties,
                                pi_entry_property_type_t property_type) {
  if (!properties) return false;
  if (property_type < 0 || property_type >= PI_ENTRY_PROPERTY_TYPE_END)
    return false;
  return properties->valid_properties & (1 << property_type);
}

pi_status_t pi_table_entry_add(const pi_dev_tgt_t dev_tgt,
                               const pi_p4_id_t table_id,
                               const pi_match_key_t *match_key,
                               const pi_table_entry_t *table_entry,
                               const int overwrite,
                               pi_entry_handle_t *entry_handle) {
  return _pi_table_entry_add(dev_tgt, table_id, match_key, table_entry,
                             overwrite, entry_handle);
}

pi_status_t pi_table_default_action_set(const pi_dev_tgt_t dev_tgt,
                                        const pi_p4_id_t table_id,
                                        const pi_table_entry_t *table_entry) {
  return _pi_table_default_action_set(dev_tgt, table_id, table_entry);
}

pi_status_t pi_table_default_action_get(const pi_dev_id_t dev_id,
                                        const pi_p4_id_t table_id,
                                        pi_table_entry_t *table_entry) {
  pi_status_t status;
  status = _pi_table_default_action_get(dev_id, table_id, table_entry);
  if (status == PI_STATUS_SUCCESS)
    table_entry->action_data->p4info = pi_get_device_p4info(dev_id);
  return status;
}

pi_status_t pi_table_default_action_done(pi_table_entry_t *table_entry) {
  return _pi_table_default_action_done(table_entry);
}

pi_status_t pi_table_entry_delete(const pi_dev_id_t dev_id,
                                  const pi_p4_id_t table_id,
                                  const pi_entry_handle_t entry_handle) {
  return _pi_table_entry_delete(dev_id, table_id, entry_handle);
}

pi_status_t pi_table_entry_modify(const pi_dev_id_t dev_id,
                                  const pi_p4_id_t table_id,
                                  const pi_entry_handle_t entry_handle,
                                  const pi_table_entry_t *table_entry) {
  return _pi_table_entry_modify(dev_id, table_id, entry_handle, table_entry);
}

pi_status_t pi_table_entries_fetch(const pi_dev_id_t dev_id,
                                   const pi_p4_id_t table_id,
                                   pi_table_fetch_res_t **res) {
  pi_table_fetch_res_t *res_ = malloc(sizeof(pi_table_fetch_res_t));
  pi_status_t status = _pi_table_entries_fetch(dev_id, table_id, res_);
  res_->p4info = pi_get_device_p4info(dev_id);
  res_->table_id = table_id;
  res_->idx = 0;
  res_->curr = 0;
  // TODO(antonin): use contiguous memory
  res_->match_keys = malloc(res_->num_entries * sizeof(pi_match_key_t));
  res_->action_datas = malloc(res_->num_entries * sizeof(pi_action_data_t));
  res_->properties = malloc(res_->num_entries * sizeof(pi_entry_properties_t));
  *res = res_;
  return status;
}

pi_status_t pi_table_entries_fetch_done(pi_table_fetch_res_t *res) {
  pi_status_t status = _pi_table_entries_fetch_done(res);
  if (status != PI_STATUS_SUCCESS) return status;

  assert(res->match_keys);
  free(res->match_keys);
  assert(res->action_datas);
  free(res->action_datas);
  assert(res->properties);
  free(res->properties);
  free(res);
  return PI_STATUS_SUCCESS;
}

size_t pi_table_entries_num(pi_table_fetch_res_t *res) {
  return res->num_entries;
}

size_t pi_table_entries_next(pi_table_fetch_res_t *res,
                             pi_table_ma_entry_t *entry,
                             pi_entry_handle_t *entry_handle) {
  if (res->idx == res->num_entries) return res->idx;

  res->curr += retrieve_entry_handle(res->entries + res->curr, entry_handle);

  entry->match_key = &res->match_keys[res->idx];
  entry->match_key->p4info = res->p4info;
  entry->match_key->table_id = res->table_id;
  entry->match_key->data_size = res->mkey_nbytes;
  entry->match_key->data = res->entries + res->curr;
  res->curr += res->mkey_nbytes;

  pi_table_entry_t *t_entry = &entry->entry;
  res->curr += retrieve_p4_id(res->entries + res->curr, &t_entry->action_id);
  uint32_t nbytes;
  res->curr += retrieve_uint32(res->entries + res->curr, &nbytes);
  t_entry->action_data = &res->action_datas[res->idx];
  t_entry->action_data->p4info = res->p4info;
  t_entry->action_data->action_id = t_entry->action_id;
  t_entry->action_data->data_size = nbytes;
  t_entry->action_data->data = res->entries + res->curr;
  res->curr += nbytes;

  pi_entry_properties_t *properties = res->properties + res->idx;
  t_entry->entry_properties = properties;
  res->curr += retrieve_uint32(res->entries + res->curr,
                               &properties->valid_properties);
  if (properties->valid_properties & (1 << PI_ENTRY_PROPERTY_TYPE_PRIORITY)) {
    res->curr += retrieve_uint32(res->entries + res->curr,
                                 &properties->priority);
  }
  if (properties->valid_properties & (1 << PI_ENTRY_PROPERTY_TYPE_TTL)) {
    res->curr += retrieve_uint32(res->entries + res->curr, &properties->ttl);
  }

  return res->idx++;
}
