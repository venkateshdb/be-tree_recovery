#include <vector>
#include <fstream>
#include <iostream>
#include <string>
#include <cassert>
#include "swap_space.hpp"

// Methods to serialize/deserialize different kinds of objects.
// You shouldn't need to touch these.
void serialize(std::iostream &fs, serialization_context &context, uint64_t x)
{
  fs << x << " ";
  assert(fs.good());
}

void deserialize(std::iostream &fs, serialization_context &context, uint64_t &x)
{
  fs >> x;
  assert(fs.good());
}

void serialize(std::iostream &fs, serialization_context &context, int64_t x)
{
  fs << x << " ";
  assert(fs.good());
}

void deserialize(std::iostream &fs, serialization_context &context, int64_t &x)
{
  fs >> x;
  assert(fs.good());
}

void serialize(std::iostream &fs, serialization_context &context, std::string x)
{
  fs << x.size() << ",";
  assert(fs.good());
  fs.write(x.data(), x.size());
  assert(fs.good());
}

void deserialize(std::iostream &fs, serialization_context &context, std::string &x)
{
  size_t length;
  char comma;
  fs >> length >> comma;
  assert(fs.good());
  char *buf = new char[length];
  assert(buf);
  fs.read(buf, length);
  assert(fs.good());
  x = std::string(buf, length);
  delete buf;
}

bool swap_space::cmp_by_last_access(swap_space::object *a, swap_space::object *b)
{
  return a->last_access < b->last_access;
}

swap_space::swap_space(backing_store *bs, uint64_t n, uint64_t checkpoint_granularity) : backstore(bs),
                                                                                         max_in_memory_objects(n),
                                                                                         checkpoint_granularity(checkpoint_granularity),
                                                                                         objects(),
                                                                                         lru_pqueue(cmp_by_last_access)
{
}

// construct a new object. Called by ss->allocate() via pointer<Referent> construction
// Does not insert into objects table - that's handled by pointer<Referent>()
swap_space::object::object(swap_space *sspace, serializable *tgt)
{
  target = tgt;
  id = sspace->next_id++;
  version = 0;
  is_leaf = false;
  refcount = 1;
  last_access = sspace->next_access_time++;
  target_is_dirty = true;
  pincount = 0;
  old_version = 0;
}

// set # of items that can live in ss.
void swap_space::set_cache_size(uint64_t sz)
{
  assert(sz > 0);
  max_in_memory_objects = sz;
  maybe_evict_something();
}

// write an object that lives on disk back to disk
// only triggers a write if the object is "dirty" (target_is_dirty == true)
void swap_space::write_back(swap_space::object *obj)
{
  assert(objects.count(obj->id) > 0);

 debug(std::cout << "Writing back " << obj->id << " " << obj->version
                  << " (" << obj->target << ") "
                  << "with last access time " << obj->last_access << std::endl);

  // This calls _serialize on all the pointers in this object,
  // which keeps refcounts right later on when we delete them all.
  // In the future, we may also use this to implement in-memory
  // evictions, i.e. where we first "evict" an object by
  // compressing it and keeping the compressed version in memory.
  serialization_context ctxt(*this);
  std::stringstream sstream;
  serialize(sstream, ctxt, *obj->target);
  obj->is_leaf = ctxt.is_leaf;

  if (obj->target_is_dirty)
  {
    std::string buffer = sstream.str();

    // modification - ss now controls BSID - split into unique id and version.
    // version increments linearly based uniquely on this version counter.

    uint64_t new_version_id = obj->version + 1;

    backstore->allocate(obj->id, new_version_id);
    std::iostream *out = backstore->get(obj->id, new_version_id);
    out->write(buffer.data(), buffer.length());
    backstore->put(out);

    // version 0 is the flag that the object exists only in memory.
    //  if (obj->version > 0)
    //    backstore->deallocate(obj->id, obj->version); // Don't want to deallocate the old version immediately
    obj->old_version = obj->version; // Store the old version
    obj->version = new_version_id;
    // for checkpointing
    object_store[obj->id] = new_version_id;
    obj->target_is_dirty = false;
  }
}

// attempt to evict an unused object from the swap space
// objects in swap space are referenced in a priority queue
// pull objects with low counts first to try and find an object with pincount 0.
void swap_space::maybe_evict_something(void)
{
  while (current_in_memory_objects > max_in_memory_objects)
  {
    object *obj = NULL;
    for (auto it = lru_pqueue.begin(); it != lru_pqueue.end(); ++it)
      if ((*it)->pincount == 0)
      {
        obj = *it;
        break;
      }
    if (obj == NULL)
      return;
    lru_pqueue.erase(obj);

    write_back(obj);

    delete obj->target;
    obj->target = NULL;
    current_in_memory_objects--;
  }
}

// Flush the dirty objects in swap_space back to disk
void swap_space::checkpoint()
{
  for (auto it = objects.begin(); it != objects.end(); ++it)
  {
    object *obj = it->second;

    if (obj->target_is_dirty)
    {
      lru_pqueue.erase(obj);

      debug(std::cout << "Checkpoint Writing back " << obj->id << "_" << obj->version
                      << " (" << obj->target << ") "
                      << "is dirty " << obj->target_is_dirty << std::endl);

      write_back(obj);
      delete obj->target;
      obj->target = NULL;
      current_in_memory_objects--;
    }
  }
}


void swap_space::deallocate_old_versions()
{
  for (auto it = objects.begin(); it != objects.end(); ++it)
  {
    object *obj = it->second;
    if (obj->old_version > 0)
    {
      debug(std::cout << "Deleting files in if " << obj->id << "_" << obj->old_version << std::endl);
      backstore->deallocate(obj->id, obj->old_version);
      obj->old_version = 0;
    }
  }
}

void swap_space::update_master_record(uint64_t lsn)
{
  std::ofstream master_record("master_record.txt", std::ios::out | std::ios::trunc);

  if (!master_record.is_open())
  {
    std::cerr << "update_master_record Failed to access master record from disk" << std::endl;
    return;
  }

  
  debug(std::cout << "LSN->" << lsn << std::endl);
  master_record << lsn << std::endl;

  debug(std::cout << "Root->" << root << std::endl);
  master_record << root << std::endl;

  for (const auto &entry : object_store)
  {
    debug(std::cout << "Update Master File" << entry.first << " " << entry.second << " " << entry.second<< std::endl);
    master_record << entry.first << ":" << entry.second << std::endl;
  }

  // master_record.close();
}


void swap_space::parse_master_log()
{
  debug(std::cout << "Inside Parse master log" << std::endl);
  std::ifstream master_log("master_record.txt");

  if (!master_log.is_open())
  {
    std::cerr << "Parse master Failed to access master record from disk" << std::endl;
    return;
  }

  // lsn
  std::string line;

  std::string lsn;
  if (std::getline(master_log, line))
  {
    std::istringstream iss(line);
    iss >> lsn;
  }

  // root
  if (std::getline(master_log, line))
  {
    std::istringstream iss(line);
    iss >> root;
  }

  // k, v
  while (std::getline(master_log, line))
  {
    std::istringstream iss(line);
    u_int64_t key, version;
    char delimiter;

    debug(std::cout << "root " << root << std::endl);
    if (iss >> key >> delimiter >> version && delimiter == ':')
    {
      debug(std::cout << " parse_master_log " << key << " " << version << std::endl);

      object *create_obj = new object(this, nullptr);
      create_obj->id = key;
      create_obj->version = version;

      object_store[key] = version;
    }
  }

  // master_log.close();
}

void swap_space::rebuild_tree()
{ 
  debug(std::cout << "Rebuilding Tree" << std::endl);

  parse_master_log();

  for(auto &entry: object_store) {
    debug(std::cout << "ID" << entry.first << std::endl);

    object *create_obj = new object(this, nullptr);
    create_obj->id = entry.first;
    create_obj->version = entry.second;

    create_obj->target_is_dirty = false;
    
    std::string loc = backstore->get_version_file(entry.first, entry.second);

    std::cout << "Reading data file " << loc << std::endl;

    std::ifstream tree_data(loc);

    if (!tree_data)
    {
      std::cerr << "Failed to data file on disk " << loc << std::endl;
      return;
    }

    std::string line;
    std::string map, pivot;

    if (std::getline(tree_data, line))
    {
      std::istringstream iss(line);
      debug(std::cout << "" << std::endl);
    }

    while (std::getline(tree_data, line))
    {
      std::istringstream iss(line);
      
      iss >> map >> pivot;

      debug(std::cout << "PIVOT" << " "  << pivot << std::endl);
      
      break;
    }

    if(!std::stoi(pivot)){
      create_obj->is_leaf = false;
    }

    objects[entry.first] = create_obj;

}
 return;
}