// {{{ headers
/*
 * Copyright (c) 2003 Thomer M. Gil
 *                    Massachusetts Institute of Technology
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef __KADEMLIA_H
#define __KADEMLIA_H

#include "p2psim/p2protocol.h"
#include "consistenthash.h"
#include <list>
#include <iostream>
using namespace std;

// }}}
// {{{ class k_nodeinfo
class k_nodeinfo {
public:
  typedef ConsistentHash::CHID NodeID;
  k_nodeinfo(NodeID, IPAddress);
  k_nodeinfo(k_nodeinfo*);
  NodeID id;
  IPAddress ip;
  Time firstts; // when we saw it first
  Time lastts;  // when we saw it last

  inline void checkrep() const;
};
// }}}
// {{{ class Kademlia
class k_bucket;
class Kademlia : public P2Protocol {
public:
  typedef ConsistentHash::CHID NodeID;

private:
// class k_nodeinfo_pool {{{
  struct k_nodeinfo_buffer
  {
    k_nodeinfo_buffer() {
      ki = 0;
      next = prev = 0;
    }

    ~k_nodeinfo_buffer() {
      delete ki;
    }

    k_nodeinfo *ki;
    k_nodeinfo_buffer *next;
    k_nodeinfo_buffer *prev;
  };

  class k_nodeinfo_pool
  {
  public:
    k_nodeinfo_pool() {
      _head = _tail = New k_nodeinfo_buffer;
      _count = 0;
      _buffer_limit = 8192;
    }

    ~k_nodeinfo_pool() {
      k_nodeinfo_buffer *i = _head;
      while(i) {
        k_nodeinfo_buffer *next = i->next;
        delete i;
        i = next;
      }
    }

    k_nodeinfo* pop(Kademlia::NodeID id, IPAddress ip)
    {
      if(!_count)
        return New k_nodeinfo(id, ip);

      k_nodeinfo *newki = _pop();
      newki->id = id;
      newki->ip = ip;
      newki->firstts = newki->lastts = 0;
      _count--;
      return newki;
    }

    k_nodeinfo* pop(k_nodeinfo *ki)
    {
      if(!_count)
        return New k_nodeinfo(ki);

      k_nodeinfo *newki = _pop();
      newki->id = ki->id;
      newki->ip = ki->ip;
      newki->firstts = ki->firstts;
      newki->lastts = ki->lastts;
      _count--;
      return newki;
    }

    void push(k_nodeinfo *ki)
    {
      // verify that this pointer isn't in here yet.
      if(Kademlia::docheckrep) {
        k_nodeinfo_buffer *i = _head;
        do {
          assert(i->ki != ki);
        } while(i->next == 0);
      }


      // allocate new space
      if(_tail->next == 0) {
        _tail->next = New k_nodeinfo_buffer;
        _tail->next->prev = _tail;
      }

      _tail = _tail->next;
      _tail->ki = ki;
      _count++;
    }

  private:
    k_nodeinfo* _pop()
    {
      _tail = _tail->prev;
      return _tail->next->ki;
    }

  private:
    unsigned _count;
    unsigned _buffer_limit;
    k_nodeinfo_buffer* _head;
    k_nodeinfo_buffer* _tail;
  };
// }}}
// {{{ public
public:
  Kademlia(Node*, Args);
  ~Kademlia();

  string proto_name() { return "Kademlia"; }
  virtual void join(Args*);
  virtual void crash(Args*);
  virtual void lookup(Args*);
  virtual void initstate(set<Protocol*>*);

  //
  // functors
  //
  class older { public:
    bool operator()(const k_nodeinfo* p1, const k_nodeinfo* p2) const {
      if(p1->id == p2->id)
        return false;
      if(p1->lastts == p2->lastts)
        return p1->id < p2->id;
      return p1->lastts < p2->lastts;
    }
  };

  class younger { public:
    bool operator()(const k_nodeinfo* p1, const k_nodeinfo* p2) const {
      if(p1->id == p2->id)
        return false;
      if(p1->lastts == p2->lastts)
        return p1->id < p2->id;
      return p1->lastts > p2->lastts;
    }
  };


  class IDcloser { public:
    bool operator()(const NodeID &n1, const NodeID &n2) const {
      DEBUG(2) << "IDcloser comparing " << Kademlia::printbits(n1) << " and " << Kademlia::printbits(n2) << ", with node = " << Kademlia::printbits(n) << endl;
      if(n1 == n2)
        return false;
      Kademlia::NodeID dist1 = Kademlia::distance(n1, n);
      Kademlia::NodeID dist2 = Kademlia::distance(n2, n);
      DEBUG(2) << "dist1 = " << Kademlia::printbits(dist1) << endl;
      DEBUG(2) << "dist2 = " << Kademlia::printbits(dist2) << endl;
      if(dist1 == dist2)
        return n1 < n2;
      return dist1 < dist2;
    }
    static NodeID n;
  };


  class closer { public:
    bool operator()(const k_nodeinfo* p1, const k_nodeinfo* p2) const {
      if(p1->id == p2->id)
        return false;
      Kademlia::NodeID dist1 = Kademlia::distance(p1->id, n);
      Kademlia::NodeID dist2 = Kademlia::distance(p2->id, n);
      if(dist1 == dist2)
        return p1->id < p2->id;
      return dist1 < dist2;
    }
    static NodeID n;
  };


  class idless { public:
    bool operator()(const k_nodeinfo* p1, const k_nodeinfo* p2) const {
      return p1->id < p2->id;
    }
  };

  //
  // static utility methods
  //
  static string printbits(NodeID);
  static string printID(NodeID id);
  static NodeID distance(const NodeID, const NodeID);
  static unsigned common_prefix(NodeID, NodeID);
  static unsigned getbit(NodeID, unsigned);
  static void reap(void*);

  //
  // non-static utility methods
  //
  // void do_lookup_wrapper(k_nodeinfo*, NodeID);

  //
  // observer methods
  //
  NodeID id() const { return _id; }
  k_bucket *root() { return _root; }
  bool stabilized(vector<NodeID>*);

  //
  // RPC arguments
  // 
  // {{{ lookup_args and lookup_result
  struct lookup_args {
    lookup_args(NodeID xid, IPAddress xip, NodeID k = 0, bool ri = false) :
      id(xid), ip(xip), key(k), return_immediately(ri), tid(threadid()) {}
    NodeID id;
    IPAddress ip;
    NodeID key;

    // if true, then do FIND_VALUE rather than LOOKUP
    bool return_immediately;
    
    // for debugging
    unsigned tid;
  };

  class lookup_result { public:
    lookup_result() { results.clear(); hops = 0; }
    ~lookup_result() {
      for(set<k_nodeinfo*, closer>::const_iterator i = results.begin(); i != results.end(); ++i)
        Kademlia::pool->push(*i);
    }
    set<k_nodeinfo*, closer> results;
    NodeID rid;     // the guy who's replying
    unsigned hops;
  };
  // }}}
  // {{{ ping_args and ping_result
  struct ping_args {
    ping_args(NodeID id, IPAddress ip) : id(id), ip(ip) {}
    NodeID id;
    IPAddress ip;
  };

  struct ping_result {
  };
  // }}}

  //
  // RPCable methods
  //
  void do_lookup(lookup_args*, lookup_result*);
  void do_ping(ping_args*, ping_result*);
  void find_node(lookup_args*, lookup_result*);


  //
  // set methods
  //

  // hack to pre-initialize k-buckets
  void init_state(list<Protocol*>);
  void reschedule_stabilizer(void*);
  friend class k_bucket;

  //
  // member variables
  //
  static bool docheckrep;
  static unsigned k;                    // number of nodes per k-bucket
  static unsigned alpha;                // alpha from kademlia paper; no of simultaneous RPCs
  static unsigned debugcounter;         // 
  static unsigned stabilize_timer;      // how often to stabilize
  static unsigned refresh_rate;         // how often to refresh info
  static k_nodeinfo_pool *pool;         // pool of k_nodeinfo_pool
  static const unsigned idsize = 8*sizeof(Kademlia::NodeID);
  hash_map<NodeID, k_nodeinfo*> flyweight;
// }}}
// {{{ private
private:
  void insert(NodeID, IPAddress, bool = false);
  void touch(NodeID);
  void erase(NodeID);
  void stabilize();

  NodeID _id;           // my id
  k_nodeinfo *_me;      // info about me
  k_bucket *_root;
  bool _joined;

  // statistics
  enum stat_type {
    STAT_LOOKUP = 0,
    STAT_PING,
    STAT_SIZE
  };
  // vector<unsigned> stat;
  // vector<unsigned> num_msgs;
  void record_stat(stat_type, unsigned, unsigned);
  friend class k_stabilizer;
  void update_k_bucket(NodeID, IPAddress);
  void clear();

  // global statistics
  static double _rpc_bytes; // total traffic
  static double _good_latency; // successful lookups
  static double _good_hops;
  static int _good_lookups;
  static int _ok_failures;
  static int _bad_failures;
  static unsigned _nkademlias;


  //
  // utility 
  //
  class callinfo { public:
    callinfo(k_nodeinfo *ki, lookup_args *la, lookup_result *lr)
      : ki(ki), la(la), lr(lr) {}
    ~callinfo() {
      Kademlia::pool->push(ki);
      delete la;
      delete lr;
    }
    k_nodeinfo *ki;
    lookup_args *la;
    lookup_result *lr;
  };

  struct reap_info {
    reap_info() {}
    ~reap_info() { delete rpcset; delete outstanding_rpcs; }

    Kademlia *k;
    RPCSet *rpcset;
    hash_map<unsigned, callinfo*>* outstanding_rpcs;
  };

  // hack for initstate
  static set<NodeID> *_all_kademlias;
  static hash_map<NodeID, Kademlia*> *_nodeid2kademlia;
// }}}
};

// }}}
// {{{ class k_nodes
class k_bucket;

/*
 * keeps a sorted set of nodes.  the size of the set never exceeds Kademlia::k.
 */
class k_nodes {
public:
  typedef set<k_nodeinfo*, Kademlia::older> nodeset_t;
  k_nodes(k_bucket *parent);
  ~k_nodes();
  void insert(Kademlia::NodeID, bool);
  void erase(Kademlia::NodeID);
  bool contains(Kademlia::NodeID) const;
  bool inrange(Kademlia::NodeID) const;
  bool full() const { return nodes.size() >= Kademlia::k; }
  bool empty() const { return !nodes.size(); }
  void clear();
  inline void checkrep(bool = true) const;

  nodeset_t nodes;

private:
  k_bucket *_parent;
  set<k_nodeinfo*, Kademlia::idless> _nodes_by_id;
};
// }}}
// {{{ class k_bucket 
class k_traverser;
class k_bucket {
public:
  k_bucket(k_bucket*, Kademlia*);
  ~k_bucket();

  Kademlia *kademlia()  { return _kademlia; }
  void traverse(k_traverser*, Kademlia*, string = "", unsigned = 0, unsigned = 0);
  void insert(Kademlia::NodeID, bool, bool = false, string = "", unsigned = 0);
  void erase(Kademlia::NodeID, string = "", unsigned = 0);
  void find_node(Kademlia::NodeID, set<k_nodeinfo*, Kademlia::closer> *, unsigned = Kademlia::k, unsigned = 0);
  void checkrep();

  void divide(unsigned);
  void collapse();

  bool leaf;

  // in case we are a leaf
  k_nodes *nodes;
  set<k_nodeinfo*, Kademlia::younger> *replacement_cache;

  // in case are a node, i.e., not a leaf
  k_bucket *child[2];

protected:
  k_bucket *parent;

private:
  Kademlia *_kademlia;

};
// }}}

// {{{ class k_traverser
class k_traverser { public:
  k_traverser(string type = "") : _type(type) {}
  virtual ~k_traverser() {}
  virtual void execute(k_bucket*, string, unsigned, unsigned) = 0;
  string type() { return _type; };

private:
  string _type;
};
// }}}
// {{{ class k_stabilizer
class k_stabilizer : public k_traverser { public:
  k_stabilizer() : k_traverser("k_stabilizer") {}
  virtual ~k_stabilizer() {}
  virtual void execute(k_bucket*, string, unsigned, unsigned);
};
// }}}
// {{{ class k_stabilized
class k_stabilized : public k_traverser { public:
  k_stabilized(vector<Kademlia::NodeID> *v) :
    k_traverser("k_stabilized"), _v(v), _stabilized(true) {}

  virtual ~k_stabilized() {}
  virtual void execute(k_bucket*, string, unsigned, unsigned);
  bool stabilized() { return _stabilized; }

private:
  vector<Kademlia::NodeID> *_v;
  bool _stabilized;
};
// }}}
// {{{ class k_finder
class k_finder : public k_traverser { public:
  k_finder(Kademlia::NodeID n) : k_traverser("k_finder"), _n(n), _found(0) {}

  virtual ~k_finder() {}
  virtual void execute(k_bucket*, string, unsigned, unsigned);
  unsigned found() { return _found; }

private:
  Kademlia::NodeID _n;
  unsigned _found;
};
// }}}
// {{{ class k_dumper
class k_dumper : public k_traverser { public:
  k_dumper() : k_traverser("k_dumper") {}
  virtual ~k_dumper() {}
  virtual void execute(k_bucket*, string, unsigned, unsigned);

private:
};
// }}}
// {{{ class k_check
class k_check : public k_traverser { public:
  k_check() : k_traverser("k_check") {}
  virtual ~k_check() {}
  virtual void execute(k_bucket*, string, unsigned, unsigned);

private:
};
// }}}


#define KDEBUG(x) DEBUG(x) << Kademlia::debugcounter++ << "(" << now() << "). " << Kademlia::printID(_id) << "(" << threadid() << ") "
#endif // __KADEMLIA_H
