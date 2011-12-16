/*
 * (C) 2007-2010 Alibaba Group Holding Limited
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * leveldb storage engine
 *
 * Version: $Id$
 *
 * Authors:
 *   nayan <nayan@taobao.com>
 *
 */

#include "storage/mdb/mdb_factory.hpp"
#include "ldb_manager.hpp"
#include "ldb_instance.hpp"

namespace tair
{
  namespace storage
  {
    namespace ldb
    {
      LdbManager::LdbManager() : cache_(NULL), scan_ldb_(NULL)
      {
        int cache_size = 0;
        bool put_fill_cache = false;
        if (TBSYS_CONFIG.getInt(TAIRLDB_SECTION, LDB_USE_CACHE, 1) > 0)
        {
          cache_size =
            TBSYS_CONFIG.getInt(TAIRLDB_SECTION, LDB_CACHE_SIZE, 256); // 256M
          cache_ = mdb_factory::create_embedded_mdb(cache_size, 1.2);
          if (NULL == cache_)
          {
            log_error("init ldb memory cache fail. cache_size: %d", cache_size);
          }
          put_fill_cache = TBSYS_CONFIG.getInt(TAIRLDB_SECTION, LDB_PUT_FILL_CACHE, 0) > 0;
        }

        bool db_version_care = TBSYS_CONFIG.getInt(TAIRLDB_SECTION, LDB_DB_VERSION_CARE, 1) > 0;
        db_count_ = TBSYS_CONFIG.getInt(TAIRLDB_SECTION, LDB_DB_INSTANCE_COUNT, 1);
        ldb_instance_ = new LdbInstance*[db_count_];
        for (int32_t i = 0; i < db_count_; ++i)
        {
          ldb_instance_[i] = new LdbInstance(i, db_version_care, cache_, put_fill_cache);
        }

        log_warn("ldb storage engine construct count: %d, db version care: %s, with cache size: %dM, put_fill_cache: %s",
                 db_count_, db_version_care ? "yes" : "no", cache_size, put_fill_cache ? "yes" : "no");
      }

      LdbManager::~LdbManager()
      {
        if (ldb_instance_ != NULL)
        {
          for (int32_t i = 0; i < db_count_; ++i)
          {
            delete ldb_instance_[i];
          }
        }

        delete[] ldb_instance_;

        if (cache_ != NULL)
        {
          delete cache_;
        }
      }

      int LdbManager::put(int bucket_number, data_entry& key, data_entry& value, bool version_care, int expire_time)
      {
        log_debug("ldb::put");
        int rc = TAIR_RETURN_SUCCESS;
        LdbInstance* db_instance = get_db_instance(bucket_number);

        if (db_instance == NULL)
        {
          log_error("ldb_bucket[%d] not exist", bucket_number);
          rc = TAIR_RETURN_FAILED;
        }
        else
        {
          rc = db_instance->put(bucket_number, key, value, version_care, expire_time);
        }

        return rc;
      }

      int LdbManager::get(int bucket_number, data_entry& key, data_entry& value)
      {
        log_debug("ldb::get");
        int rc = TAIR_RETURN_SUCCESS;
        LdbInstance* db_instance = get_db_instance(bucket_number);

        if (db_instance == NULL)
        {
          log_error("ldb_bucket[%d] not exist", bucket_number);
          rc = TAIR_RETURN_FAILED;
        }
        else
        {
          rc = db_instance->get(bucket_number, key, value);
        }

        return rc;
	  }

	  int LdbManager::get_target_kv(int32_t area, int bucket_number,data_entry & key,data_entry & end_key,data_entry & value,leveldb::Iterator* &iter,int expire_time)
	  {
		  log_debug("ldb::get_target_kv");
		  int rc = TAIR_RETURN_SUCCESS;
		  LdbKey ldb_key;
		  LdbItem ldb_item;
		  LdbInstance* db_instance = get_db_instance(bucket_number);
		  if (db_instance == NULL)
		  {
			  log_error("ldb_bucket[%d] not exist", bucket_number);
			  rc = TAIR_RETURN_FAILED;
		  }
		  else {
			  if(iter == NULL){
				  //get iter
				  log_debug("get_target_kv:get iter");
				  //leveldb::Slice in(key.get_data(), key.get_size()), out;
				  //LookupKey * lookup = db_instance->get_data(bucket_number, in, out);
				  //log_debug("==================%d out key:%s", out.size(), out.data()); 

				  //iter = db_instance->seek_key(out.data(), out.size());
				  iter = db_instance->seek_key(area, key.get_data(), key.get_size(), bucket_number);
				  log_debug("seek key:%s, size:%d", key.get_data()+4, key.get_size());
			  }

			  while(iter->Valid()){
				  ldb_key.assign(const_cast<char*>(iter->key().data()), iter->key().size());
				  ldb_item.assign(const_cast<char*>(iter->value().data()), iter->value().size());

				  //get bucket number
				  int bucket_num = ldb_key.get_bucket_number();
				  log_debug("*************current bucket:%d, the key bucket:%d", bucket_number, bucket_num);
				  if(bucket_number != bucket_num){
					  rc = TAIR_RETURN_END_ERROR;
					  return rc;
				  }

				  //remove area
				  data_entry nkey(ldb_key.key(), ldb_key.key_size(), false);
				  //nkey has merge area,so set it to true
				  nkey.has_merged = true;
				  log_debug("key:%s, %d,%d", ldb_key.key()+4, ldb_key.key()[0], ldb_key.key()[1]);
				  int key_area = nkey.decode_area();
				  key.clone(nkey);
				  log_debug("*************current area:%d, the key area:%d", area, key_area);
				  if(area != key_area){
					  rc = TAIR_RETURN_END_ERROR;
					  return rc;
				  }

				  data_entry nvalue(ldb_item.value(), ldb_item.value_size(), false);
				  value.clone(nvalue);

				  key.data_meta.mdate = ldb_item.meta().mdate_;
				  key.data_meta.cdate = ldb_item.meta().cdate_;
				  key.data_meta.edate = ldb_item.meta().edate_;
				  key.data_meta.version = ldb_item.meta().version_;

				  log_debug("========get==========%d key:%s", key.get_size(), key.get_data() +2); 
				  log_debug("========get==========%d value:%s", value.get_size(), value.get_data() +2); 
				  break;
			  }
			  if(!iter->Valid()){
				  rc = TAIR_RETURN_END_ERROR;
				  return rc;
			  }
			  iter->Next();
		  }

		  return rc;
	  }

	  int LdbManager::remove(int bucket_number, data_entry& key, bool version_care)
	  {
		  log_debug("ldb::remove");
		  int rc = TAIR_RETURN_SUCCESS;
		  LdbInstance* db_instance = get_db_instance(bucket_number);

		  if (db_instance == NULL)
		  {
			  log_error("ldb_bucket[%d] not exist", bucket_number);
			  rc = TAIR_RETURN_FAILED;
		  }
		  else
		  {
			  rc = db_instance->remove(bucket_number, key, version_care);
		  }

		  return rc;
	  }

	  int LdbManager::clear(int32_t area)
	  {
		  log_debug("ldb::clear %d", area);
		  int ret = TAIR_RETURN_SUCCESS;
		  for (int32_t i = 0; i < db_count_; ++i)
		  {
			  int tmp_ret = ldb_instance_[i]->clear_area(area);
			  if (tmp_ret != TAIR_RETURN_SUCCESS)
			  {
				  ret = tmp_ret;
				  log_error("clear area %d for instance %d fail.", area, i); // just continue
			  }
		  }
		  return ret;
	  }

	  bool LdbManager::init_buckets(const std::vector<int>& buckets)
	  {
		  log_debug("ldb::init buckets");
		  tbsys::CThreadGuard guard(&lock_);
		  bool ret = false;

		  if (1 == db_count_)
		  {
			  ret = ldb_instance_[0]->init_buckets(buckets);
		  }
		  else
		  {
			  std::vector<int32_t>* tmp_buckets = new std::vector<int32_t>[db_count_];

			  for (std::vector<int>::const_iterator it = buckets.begin(); it != buckets.end(); ++it)
			  {
				  tmp_buckets[*it % db_count_].push_back(*it);
			  }

			  for (int32_t i = 0; i < db_count_; ++i)
			  {
				  ret = ldb_instance_[i]->init_buckets(tmp_buckets[i]);
				  if (!ret)
				  {
					  log_error("init buckets for db instance %d", i);
					  break;
				  }
			  }
			  delete tmp_buckets;
		  }

		  return ret;
	  }

	  void LdbManager::close_buckets(const std::vector<int>& buckets)
	  {
		  log_debug("ldb::close buckets");
		  tbsys::CThreadGuard guard(&lock_);
		  if (1 == db_count_)
		  {
			  ldb_instance_[0]->close_buckets(buckets);
		  }
		  else
		  {
			  std::vector<int32_t>* tmp_buckets = new std::vector<int32_t>[db_count_];
			  for (std::vector<int>::const_iterator it = buckets.begin(); it != buckets.end(); ++it)
			  {
				  tmp_buckets[*it % db_count_].push_back(*it);
			  }

			  for (int32_t i = 0; i < db_count_; ++i)
			  {
				  ldb_instance_[i]->close_buckets(tmp_buckets[i]);
			  }
			  delete tmp_buckets;
		  }
	  }

	  // only one bucket can scan at any time ?
	  void LdbManager::begin_scan(md_info& info)
	  {
		  if ((scan_ldb_ = get_db_instance(info.db_id)) == NULL)
		  {
			  log_error("scan bucket[%u] not exist", info.db_id);
		  }
		  else
		  {
			  if (!scan_ldb_->begin_scan(info.db_id))
			  {
				  log_error("begin scan bucket[%u] fail", info.db_id);
			  }
		  }
	  }

	  void LdbManager::end_scan(md_info& info)
	  {
		  if (scan_ldb_ != NULL)
		  {
			  scan_ldb_->end_scan();
			  scan_ldb_ = NULL;
		  }
	  }

	  bool LdbManager::get_next_items(md_info& info, std::vector<item_data_info*>& list)
	  {
		  bool ret = true;
		  if (NULL == scan_ldb_)
		  {
			  ret = false;
			  log_error("scan bucket not opened");
		  }
		  else
		  {
			  ret = scan_ldb_->get_next_items(list);
			  log_debug("get items %d", list.size());
		  }
		  return ret;
	  }

	  void LdbManager::set_area_quota(int area, uint64_t quota)
	  {
		  // we consider set area quota to cache
		  if (cache_ != NULL)
		  {
			  cache_->set_area_quota(area, quota);
		  }
	  }

	  void LdbManager::set_area_quota(std::map<int, uint64_t>& quota_map)
	  {
		  if (cache_ != NULL)
		  {
			  cache_->set_area_quota(quota_map);
		  }
	  }

	  void LdbManager::get_stats(tair_stat* stat)
	  {
		  tbsys::CThreadGuard guard(&lock_);
		  log_debug("ldbmanager get stats");

		  for (int32_t i = 0; i < db_count_; ++i)
		  {
			  ldb_instance_[i]->get_stats(stat);
		  }
	  }

	  LdbInstance* LdbManager::get_db_instance(const int bucket_number)
	  {
		  LdbInstance* ret = ldb_instance_[bucket_number % db_count_];
		  assert(ret != NULL);
		  return ret->exist(bucket_number) ? ret : NULL;
	  }

	}
  }
}
