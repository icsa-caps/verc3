/*
 * Copyright (c) 2016, The University of Edinburgh
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/

#include <cstddef>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

#include <glog/logging.h>

#include "verc3/core/model_checking.hh"
#include "verc3/core/types.hh"
#include "verc3/debug.hh"
#include "verc3/util.hh"

using namespace verc3;

namespace {

constexpr std::size_t PROC_COUNT = 3;
constexpr std::size_t NET_MAX = PROC_COUNT + 1;

typedef core::WeakUnion Node;

struct Msg {
  enum class Type {
    GetS,
    GetM,
    PutS,
    PutM,
    Fwd_GetS,
    Fwd_GetM,
    Inv,
    Put_Ack,
    Data,
    Inv_Ack
  };

  struct Hash {
    std::size_t operator()(const Msg& k) const {
      auto h = core::Hasher<decltype(k.mtype)>::type()(k.mtype);
      util::CombineHash(k.src.id_as<int>(), &h);
      util::CombineHash(k.need_acks, &h);
      return h;
    }
  };

  bool operator==(const Msg& rhs) const {
    return mtype == rhs.mtype && src == rhs.src && need_acks == rhs.need_acks;
  }

  friend std::ostream& operator<<(std::ostream& os, const Msg& msg) {
    os << "(";

    switch (msg.mtype) {
      case Type::GetS:
        os << "GetS";
        break;
      case Type::GetM:
        os << "GetM";
        break;
      case Type::PutS:
        os << "PutS";
        break;
      case Type::PutM:
        os << "PutM";
        break;
      case Type::Fwd_GetS:
        os << "Fwd_GetS";
        break;
      case Type::Fwd_GetM:
        os << "Fwd_GetM";
        break;
      case Type::Inv:
        os << "Inv";
        break;
      case Type::Put_Ack:
        os << "Put_Ack";
        break;
      case Type::Data:
        os << "Data";
        break;
      case Type::Inv_Ack:
        os << "Inv_Ack";
        break;
    }

    os << ", Node_" << msg.src.id_as<int>() << ", " << msg.need_acks << ")";

    return os;
  }

  friend struct MachineState;

 private:
  Type mtype;
  Node src;
  int need_acks;
};

typedef core::ArraySet<Msg, NET_MAX> UnorderChan;
typedef std::list<Msg> OrderChan;

struct L1 {
  typedef core::ArraySet<L1, PROC_COUNT> ScalarSet;

  enum class State {
    I,
    S,
    M,
    IS_D,
    IM_AD,
    IM_A,
    SM_AD,
    SM_A,
    MI_A,
    SI_A,
    II_A,
  };

  struct Hash {
    std::size_t operator()(const L1& k) const {
      auto h = core::Hasher<decltype(k.state)>::type()(k.state);
      util::CombineHash(k.need_acks, &h);
      util::CombineHash(k.chan, &h);
      for (const auto& msg : k.fwd_chan) {
        util::CombineHash(msg, &h);
      }
      return h;
    }
  };

  bool operator==(const L1& rhs) const {
    return state == rhs.state && need_acks == rhs.need_acks &&
           chan == rhs.chan && fwd_chan == rhs.fwd_chan;
  }

  friend std::ostream& operator<<(std::ostream& os, const L1& l1) {
    os << " | state = ";

    switch (l1.state) {
      case State::I:
        os << "I";
        break;
      case State::S:
        os << "S";
        break;
      case State::M:
        os << "M";
        break;
      case State::IS_D:
        os << "IS_D";
        break;
      case State::IM_AD:
        os << "IM_AD";
        break;
      case State::IM_A:
        os << "IM_A";
        break;
      case State::SM_AD:
        os << "SM_AD";
        break;
      case State::SM_A:
        os << "SM_A";
        break;
      case State::MI_A:
        os << "MI_A";
        break;
      case State::SI_A:
        os << "SI_A";
        break;
      case State::II_A:
        os << "II_A";
        break;
    }

    os << std::endl;

    os << " | need_acks = " << l1.need_acks << std::endl;

    os << " | chan = { ";
    l1.chan.for_each([&os](const Msg& msg) { os << msg << ", "; });
    os << "}" << std::endl;

    os << " | fwd_chan = [ ";
    for (const auto& msg : l1.fwd_chan) {
      os << msg << ", ";
    }
    os << "]" << std::endl;

    return os;
  }

 public:
  State state = State::I;
  int need_acks = 0;
  UnorderChan chan;
  OrderChan fwd_chan;
};

struct Dir {
  enum class State { I, S, M, S_D };

  typedef core::ArraySet<Dir, 1, L1::ScalarSet> ScalarSet;

  struct Hash {
    std::size_t operator()(const Dir& k) const {
      auto h = core::Hasher<decltype(k.state)>::type()(k.state);
      util::CombineHash(k.owner, &h);
      util::CombineHash(k.sharers, &h);
      util::CombineHash(k.chan, &h);
      return h;
    }
  };

  bool operator==(const Dir& rhs) const {
    return state == rhs.state && owner == rhs.owner && sharers == rhs.sharers &&
           chan == rhs.chan;
  }

  friend std::ostream& operator<<(std::ostream& os, const Dir& dir) {
    os << " | state = ";

    switch (dir.state) {
      case State::I:
        os << "I";
        break;
      case State::S:
        os << "S";
        break;
      case State::M:
        os << "M";
        break;
      case State::S_D:
        os << "S_D";
        break;
    }

    os << std::endl;

    os << " | owner = L1[Node_" << static_cast<int>(dir.owner) << "]"
       << std::endl;

    os << " | sharers = { ";
    dir.sharers.for_each([&os](L1::ScalarSet::ID id) {
      os << "L1[Node_" << static_cast<int>(id) << "], ";
    });
    os << "}" << std::endl;

    os << " | chan = { ";
    dir.chan.for_each([&os](const Msg& msg) { os << msg << ", "; });
    os << "}" << std::endl;

    return os;
  }

 public:
  State state = State::I;
  L1::ScalarSet::ID owner = L1::ScalarSet::ID::kUndefined;
  core::ArraySet<L1::ScalarSet::ID, PROC_COUNT> sharers;
  UnorderChan chan;
};

struct MachineState : core::StateNonAccepting {
  struct Hash {
    std::size_t operator()(const MachineState& k) const {
      auto h = core::Hasher<decltype(k.l1caches)>::type()(k.l1caches);
      util::CombineHash(k.dir, &h);
      return h;
    }
  };

  bool operator==(const MachineState& rhs) const {
    return l1caches == rhs.l1caches && dir == rhs.dir;
  }

  MachineState() : l1caches(true), dir(true) {}

  friend std::ostream& operator<<(std::ostream& os, const MachineState& m) {
    m.l1caches.for_each_ID([&os, &m](L1::ScalarSet::ID id) {
      os << " +---< L1[Node_" << static_cast<int>(id) << "] >" << std::endl;
      os << *m.l1caches[id];
    });

    m.dir.for_each_ID([&os, &m](Dir::ScalarSet::ID id) {
      os << " +---< Dir[Node_" << static_cast<int>(id) << "] >" << std::endl;
      os << *m.dir[id];
    });

    return os;
  }

  Dir::ScalarSet::ID Dir0() const { return dir.NextValid(); }

  void Send(Msg::Type mtype, Node dst, Node src, int need_acks = 0) {
    UnorderChan* chan;

    if (l1caches.IsMember(dst.id_as<L1::ScalarSet::ID>())) {
      chan = &l1caches[dst.id_as<L1::ScalarSet::ID>()]->chan;
    } else {
      assert(dir.IsMember(dst.id_as<Dir::ScalarSet::ID>()));
      chan = &dir[dst.id_as<Dir::ScalarSet::ID>()]->chan;
    }

    auto id = chan->NextInvalid();
    core::ErrorIf(id == UnorderChan::ID::kUndefined, "Too many messages");

    (*chan)[id]->mtype = mtype;
    (*chan)[id]->src = src;
    (*chan)[id]->need_acks = need_acks;
  }

  void SendOrdered(Msg::Type mtype, Node dst, Node src, int need_acks = 0) {
    OrderChan* chan;
    assert(l1caches.IsMember(dst.id_as<L1::ScalarSet::ID>()));
    chan = &l1caches[dst.id_as<L1::ScalarSet::ID>()]->fwd_chan;

    Msg msg;
    msg.mtype = mtype;
    msg.src = src;
    msg.need_acks = need_acks;
    chan->emplace_back(std::move(msg));
  }

  void ErrorUnhandledMsg(Node id) {
    std::ostringstream oss;
    oss << "Unhandled message type @ Node_" << id.id_as<int>() << " !";
    throw core::Error(oss.str());
  }

  void AddToSharersList(L1::ScalarSet::ID id) {
    if (!dir()->sharers.Contains(id)) {
      dir()->sharers.Insert(id);
    }
  }

  void RemoveFromSharersList(L1::ScalarSet::ID id) { dir()->sharers.Erase(id); }

  void BCastInv_ClearSharers(Node src) {
    dir()->sharers.for_each([&src, this](const L1::ScalarSet::ID& id) {
      if (id != src.id_as<L1::ScalarSet::ID>()) {
        SendOrdered(Msg::Type::Inv, Node(id), src);
      }
    });
    dir()->sharers.Clear();
  }

  bool DirectoryReceive(const Msg& msg) {
    auto num_sharers = dir()->sharers.size();
    if (dir()->sharers.Contains(msg.src.id_as<L1::ScalarSet::ID>())) {
      --num_sharers;
    }

    switch (dir()->state) {
      case Dir::State::I:
        core::ErrorIf(num_sharers != 0, "Sharers list non-empty but line in I");

        switch (msg.mtype) {
          case Msg::Type::GetS:
            Send(Msg::Type::Data, msg.src, Node(Dir0()));
            AddToSharersList(msg.src.id_as<L1::ScalarSet::ID>());
            dir()->state = Dir::State::S;
            break;

          case Msg::Type::GetM:
            Send(Msg::Type::Data, msg.src, Node(Dir0()));
            dir()->owner = msg.src.id_as<L1::ScalarSet::ID>();
            dir()->state = Dir::State::M;
            break;

          case Msg::Type::PutS:
          case Msg::Type::PutM:
            SendOrdered(Msg::Type::Put_Ack, msg.src, Node(Dir0()));
            break;

          default:
            ErrorUnhandledMsg(Node(Dir0()));
        }
        break;

      case Dir::State::S:
        switch (msg.mtype) {
          case Msg::Type::GetS:
            AddToSharersList(msg.src.id_as<L1::ScalarSet::ID>());
            Send(Msg::Type::Data, msg.src, Node(Dir0()));
            break;

          case Msg::Type::GetM:
            Send(Msg::Type::Data, msg.src, Node(Dir0()), num_sharers);
            BCastInv_ClearSharers(msg.src);
            dir()->owner = msg.src.id_as<L1::ScalarSet::ID>();
            dir()->state = Dir::State::M;
            dir()->sharers.Clear();  // undefine
            break;

          case Msg::Type::PutS:
            RemoveFromSharersList(msg.src.id_as<L1::ScalarSet::ID>());
            SendOrdered(Msg::Type::Put_Ack, msg.src, Node(Dir0()));
            if (dir()->sharers.empty()) {
              dir()->state = Dir::State::I;
            }
            break;

          case Msg::Type::PutM:
            RemoveFromSharersList(msg.src.id_as<L1::ScalarSet::ID>());
            SendOrdered(Msg::Type::Put_Ack, msg.src, Node(Dir0()));
            break;

          default:
            ErrorUnhandledMsg(Node(Dir0()));
        }
        break;

      case Dir::State::M:
        core::ErrorIf(dir()->owner == L1::ScalarSet::ID::kUndefined,
                      "dir has no owner, but line is Modified");

        switch (msg.mtype) {
          case Msg::Type::GetS:
            SendOrdered(Msg::Type::Fwd_GetS, Node(dir()->owner), msg.src);
            AddToSharersList(msg.src.id_as<L1::ScalarSet::ID>());
            AddToSharersList(dir()->owner);
            dir()->owner = L1::ScalarSet::ID::kUndefined;  // undefine
            dir()->state = Dir::State::S_D;
            break;

          case Msg::Type::GetM:
            SendOrdered(Msg::Type::Fwd_GetM, Node(dir()->owner), msg.src);
            dir()->owner = msg.src.id_as<L1::ScalarSet::ID>();
            break;

          case Msg::Type::PutS:
            SendOrdered(Msg::Type::Put_Ack, msg.src, Node(Dir0()));
            break;

          case Msg::Type::PutM:
            SendOrdered(Msg::Type::Put_Ack, msg.src, Node(Dir0()));
            if (msg.src.id_as<L1::ScalarSet::ID>() == dir()->owner) {
              dir()->owner = L1::ScalarSet::ID::kUndefined;
              dir()->state = Dir::State::I;
            }
            break;

          default:
            ErrorUnhandledMsg(Node(Dir0()));
        }
        break;

      case Dir::State::S_D:
        switch (msg.mtype) {
          case Msg::Type::GetS:
          case Msg::Type::GetM:
            return false;

          case Msg::Type::PutS:
          case Msg::Type::PutM:
            RemoveFromSharersList(msg.src.id_as<L1::ScalarSet::ID>());
            SendOrdered(Msg::Type::Put_Ack, msg.src, Node(Dir0()));
            break;

          case Msg::Type::Data:
            dir()->state = Dir::State::S;
            break;

          default:
            ErrorUnhandledMsg(Node(Dir0()));
        }
        break;
    }

    return true;
  }

  bool L1CacheReceive(L1::ScalarSet::ID id, const Msg& msg) {
    auto l1 = l1caches[id];

    switch (l1->state) {
      case L1::State::I:
        ErrorUnhandledMsg(Node(id));
        break;

      case L1::State::IS_D:
        switch (msg.mtype) {
          case Msg::Type::Inv:
            return false;

          case Msg::Type::Data:
            l1->state = L1::State::S;
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::IM_AD:
        switch (msg.mtype) {
          case Msg::Type::Fwd_GetS:
          case Msg::Type::Fwd_GetM:
            return false;

          case Msg::Type::Data:
            if (msg.src.id_as<Dir::ScalarSet::ID>() == Dir0() &&
                msg.need_acks) {
              l1->need_acks += msg.need_acks;
              if (l1->need_acks == 0) {
                l1->state = L1::State::M;
              } else {
                l1->state = L1::State::IM_A;
              }
            } else {
              l1->state = L1::State::M;
            }
            break;

          case Msg::Type::Inv_Ack:
            --l1->need_acks;
            if (l1->need_acks == 0) {
              l1->state = L1::State::M;
            }
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::IM_A:
        switch (msg.mtype) {
          case Msg::Type::Fwd_GetS:
          case Msg::Type::Fwd_GetM:
            return false;

          case Msg::Type::Inv_Ack:
            if (--l1->need_acks == 0) {
              l1->state = L1::State::M;
            }
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::S:
        switch (msg.mtype) {
          case Msg::Type::Inv:
            Send(Msg::Type::Inv_Ack, msg.src, Node(id));
            l1->state = L1::State::I;
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::SM_AD:
        switch (msg.mtype) {
          case Msg::Type::Fwd_GetS:
          case Msg::Type::Fwd_GetM:
            return false;

          case Msg::Type::Inv:
            Send(Msg::Type::Inv_Ack, msg.src, Node(id));
            l1->state = L1::State::IM_AD;
            break;

          case Msg::Type::Data:
            if (msg.src.id_as<Dir::ScalarSet::ID>() == Dir0() &&
                msg.need_acks) {
              l1->need_acks += msg.need_acks;
              if (l1->need_acks == 0) {
                l1->state = L1::State::M;
              } else {
                l1->state = L1::State::SM_A;
              }
            } else {
              l1->state = L1::State::M;
            }
            break;

          case Msg::Type::Inv_Ack:
            --l1->need_acks;
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::SM_A:
        switch (msg.mtype) {
          case Msg::Type::Fwd_GetS:
          case Msg::Type::Fwd_GetM:
            return false;

          case Msg::Type::Inv_Ack:
            if (--l1->need_acks == 0) {
              l1->state = L1::State::M;
            }
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::M:
        switch (msg.mtype) {
          case Msg::Type::Fwd_GetS:
            Send(Msg::Type::Data, msg.src, Node(id));
            Send(Msg::Type::Data, Node(Dir0()), Node(id));
            l1->state = L1::State::S;
            break;

          case Msg::Type::Fwd_GetM:
            Send(Msg::Type::Data, msg.src, Node(id));
            l1->state = L1::State::I;
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::MI_A:
        switch (msg.mtype) {
          case Msg::Type::Fwd_GetS:
            Send(Msg::Type::Data, msg.src, Node(id));
            Send(Msg::Type::Data, Node(Dir0()), Node(id));
            l1->state = L1::State::SI_A;
            break;

          case Msg::Type::Fwd_GetM:
            Send(Msg::Type::Data, msg.src, Node(id));
            l1->state = L1::State::II_A;
            break;

          case Msg::Type::Put_Ack:
            l1->state = L1::State::I;
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::SI_A:
        switch (msg.mtype) {
          case Msg::Type::Put_Ack:
            l1->state = L1::State::I;
            break;

          case Msg::Type::Inv:
            Send(Msg::Type::Inv_Ack, msg.src, Node(id));
            l1->state = L1::State::II_A;
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;

      case L1::State::II_A:
        switch (msg.mtype) {
          case Msg::Type::Put_Ack:
            l1->state = L1::State::I;
            break;

          default:
            ErrorUnhandledMsg(Node(id));
        }
        break;
    }

    return true;
  }

 public:
  L1::ScalarSet l1caches;
  Dir::ScalarSet dir;
};

class L1Action : public core::Rule<MachineState> {
 public:
  explicit L1Action(std::string name, L1::ScalarSet::ID id)
      : core::Rule<MachineState>(std::move(name)), id_(id) {
    std::ostringstream oss;
    oss << "L1[Node_" << static_cast<int>(id_) << "]:" << name_;
    name_ = oss.str();
  }

  auto id() const { return id_; }

 private:
  L1::ScalarSet::ID id_;
};

struct I_Read : L1Action {
  explicit I_Read(L1::ScalarSet::ID id) : L1Action("I.Read", id) {}

  bool PreCond(const MachineState& state) const override {
    return state.l1caches[id()]->state == L1::State::I;
  }

  bool Action(MachineState* state) const override {
    state->Send(Msg::Type::GetS, Node(state->Dir0()), Node(id()));
    state->l1caches[id()]->state = L1::State::IS_D;
    return true;
  }
};

struct I_Write : L1Action {
  explicit I_Write(L1::ScalarSet::ID id) : L1Action("I.Write", id) {}

  bool PreCond(const MachineState& state) const override {
    return state.l1caches[id()]->state == L1::State::I;
  }

  bool Action(MachineState* state) const override {
    state->Send(Msg::Type::GetM, Node(state->Dir0()), Node(id()));
    state->l1caches[id()]->state = L1::State::IM_AD;
    return true;
  }
};

struct S_Write : L1Action {
  explicit S_Write(L1::ScalarSet::ID id) : L1Action("S.Write", id) {}

  bool PreCond(const MachineState& state) const override {
    return state.l1caches[id()]->state == L1::State::S;
  }

  bool Action(MachineState* state) const override {
    state->Send(Msg::Type::GetM, Node(state->Dir0()), Node(id()));
    state->l1caches[id()]->state = L1::State::SM_AD;
    return true;
  }
};

struct S_Replacement : L1Action {
  explicit S_Replacement(L1::ScalarSet::ID id)
      : L1Action("S.Replacement", id) {}

  bool PreCond(const MachineState& state) const override {
    return state.l1caches[id()]->state == L1::State::S;
  }

  bool Action(MachineState* state) const override {
    state->Send(Msg::Type::PutS, Node(state->Dir0()), Node(id()));
    state->l1caches[id()]->state = L1::State::SI_A;
    return true;
  }
};

struct M_Replacement : L1Action {
  explicit M_Replacement(L1::ScalarSet::ID id)
      : L1Action("M.Replacement", id) {}

  bool PreCond(const MachineState& state) const override {
    return state.l1caches[id()]->state == L1::State::M;
  }

  bool Action(MachineState* state) const override {
    state->Send(Msg::Type::PutM, Node(state->Dir0()), Node(id()));
    state->l1caches[id()]->state = L1::State::MI_A;
    return true;
  }
};

class L1ReceiveUnordered : public L1Action {
 public:
  explicit L1ReceiveUnordered(L1::ScalarSet::ID id, UnorderChan::ID msg_id)
      : L1Action("L1ReceiveUnordered", id), msg_id_(msg_id) {}

  bool PreCond(const MachineState& state) const override {
    return state.l1caches[id()]->chan.IsValid(msg_id_);
  }

  bool Action(MachineState* state) const override {
    auto msg = state->l1caches[id()]->chan[msg_id_];
    if (state->L1CacheReceive(id(), *msg)) {
      state->l1caches[id()]->chan.SetInvalid(msg_id_);
      return true;
    }

    return false;
  }

 private:
  UnorderChan::ID msg_id_;
};

class L1ReceiveOrdered : public L1Action {
 public:
  explicit L1ReceiveOrdered(L1::ScalarSet::ID id)
      : L1Action("L1ReceiveUnordered", id) {}

  bool PreCond(const MachineState& state) const override {
    return !state.l1caches[id()]->fwd_chan.empty();
  }

  bool Action(MachineState* state) const override {
    auto& msg = state->l1caches[id()]->fwd_chan.front();
    if (state->L1CacheReceive(id(), msg)) {
      state->l1caches[id()]->fwd_chan.pop_front();
      return true;
    }

    return false;
  }
};

class DirReceive : public core::Rule<MachineState> {
 public:
  explicit DirReceive(UnorderChan::ID msg_id)
      : core::Rule<MachineState>("DirReceive"), msg_id_(msg_id) {}

  bool PreCond(const MachineState& state) const override {
    return state.dir()->chan.IsValid(msg_id_);
  }

  bool Action(MachineState* state) const override {
    auto msg = state->dir()->chan[msg_id_];
    if (state->DirectoryReceive(*msg)) {
      state->dir()->chan.SetInvalid(msg_id_);
      return true;
    }

    return false;
  }

 private:
  UnorderChan::ID msg_id_;
};

/**
 * Livelock freedom liveness property.
 *
 * If a processor issues a memory access, this memory access must eventually be
 * satisfied [1]. To assert this is true, this class records the state graph
 * (for each processor individually) between transient state. If there exists a
 * cycle in the transient state graph, it is possible to never reach a stable
 * state again.
 *
 * [1] <a href="http://www.kenmcmil.com/pubs/ISSMM91.pdf"> Kenneth L. McMillan,
 *      J. Schwalbe, "Formal verification of the Gigamax cache consistency
 *      protocol" ISSM. 1991</a>
 */
class LivelockFreedom : public core::Property<MachineState> {
 public:
  explicit LivelockFreedom() : Property<MachineState>("LivelockFreedom") {}

  bool Invariant(const MachineState& state) const override { return true; }

  void Next(const MachineState& state,
            const core::StateMap<MachineState>& next_states) override {
    for (const auto& kv : next_states) {
      // We want each cache to always eventually reach a stable state. For now
      // this only checks that a L1 cache does not ping-pong between some
      // transient states.
      state.l1caches.for_each_ID(
          [this, &state, &kv](const L1::ScalarSet::ID id) {
            auto prev_l1 = state.l1caches[id];
            auto next_l1 = kv.second.l1caches[id];
            if (!IsStable(prev_l1->state) && !IsStable(next_l1->state) &&
                prev_l1->state != next_l1->state) {
              auto idx = static_cast<std::size_t>(id) - L1::ScalarSet::kBase;
              state_graphs_[idx].Insert(*prev_l1, *next_l1);
            }
          });
    }
  }

  bool IsSatisfied(core::Relation<L1>::Path* path = nullptr) const {
    for (const auto& g : state_graphs_) {
      if (!g.Acyclic(path)) {
        return false;
      }
    }
    return true;
  }

 private:
  bool IsStable(L1::State s) {
    return s == L1::State::I || s == L1::State::S || s == L1::State::M;
  }

  std::array<core::Relation<L1>, PROC_COUNT> state_graphs_;
};

core::TransitionSystem<MachineState> TransitionSystem(const MachineState& s) {
  core::TransitionSystem<MachineState> ts;

  // Transitions

  s.l1caches.for_each_ID([&ts, &s](L1::ScalarSet::ID id) {
    ts.Make<I_Read>(id);
    ts.Make<I_Write>(id);
    ts.Make<S_Write>(id);
    ts.Make<S_Replacement>(id);
    ts.Make<M_Replacement>(id);

    s.l1caches[id]->chan.for_each_ID([&ts, &id](UnorderChan::ID msg_id) {
      ts.Make<L1ReceiveUnordered>(id, msg_id);
    });
    ts.Make<L1ReceiveOrdered>(id);
  });

  s.dir()->chan.for_each_ID(
      [&ts](UnorderChan::ID msg_id) { ts.Make<DirReceive>(msg_id); });

  // Invariants

  ts.Make<core::InvariantF<MachineState>>(
      "SWMR", [](const MachineState& state) {
        return state.l1caches.all_of([&state](const L1& c1) {
          return state.l1caches.all_of([&c1](const L1& c2) {
            if (&c1 != &c2 && c1.state == L1::State::M) {
              return c2.state != L1::State::M && c2.state != L1::State::S;
            }
            return true;
          });
        });
      });

  ts.Make<LivelockFreedom>();

  return ts;
}

}  // namespace

namespace models {

/**
 * Implements and model checks the MSI directory cache coherence protocol
 * described in: D. J. Sorin, M. D. Hill and D. A. Wood "A Primer on Memory
 * Consistency and Cache Coherence", 2011 [Sec. 8.2.4.].
 */
int Main_msi_directory(int argc, char* argv[]) {
  MachineState s;
  auto ts = TransitionSystem(s);

  std::unique_ptr<core::EvalBase<decltype(ts)>> eval;
  if (argc > 1 && std::string(argv[1]) == "hashing") {
    LOG(INFO) << "Using evaluation backend: Eval_BFS_Hashing";
    eval.reset(new core::Eval_BFS_Hashing<decltype(ts)>());
  } else {
    LOG(INFO) << "Using evaluation backend: Eval_BFS";
    eval.reset(new core::Eval_BFS<decltype(ts)>());
  }

  eval->set_monitor([](
      const core::EvalBase<core::TransitionSystem<MachineState>>& mc,
      core::StateQueue<MachineState>* accept) {
    VLOG_EVERY_N(0, 10000) << "... visited states: " << mc.num_visited_states()
                           << " | queued: " << mc.num_queued_states();
    return true;
  });

  try {
    eval->Evaluate({s}, &ts);
  } catch (const core::EvalBase<decltype(ts)>::ExceptionTrace& trace) {
    std::cout << std::endl;
    debug::PrintTraceDiff(
        trace.trace(),
        [](const decltype(eval)::element_type::Trace::value_type& state_rule,
           std::ostream& os) { os << state_rule.first; },
        [](const decltype(eval)::element_type::Trace::value_type& state_rule,
           std::ostream& os) {
          if (!state_rule.second.empty()) {
            os << "\e[1;32m================( " << state_rule.second
               << " )===>\e[0m" << std::endl;
          }
        },
        std::cout);

    std::cout << "\e[1;31m===> VERIFICATION FAILED (" << trace.trace().size()
              << " steps): " << trace.error().what() << "\e[0m" << std::endl;
    std::cout << std::endl;
    return 1;
  } catch (const std::bad_alloc& e) {
    LOG(ERROR) << "Out of memory!";
    return 42;
  }

  for (const auto& prop : ts.properties()) {
    auto property = dynamic_cast<LivelockFreedom*>(prop.get());
    if (property != nullptr) {
      core::Relation<L1>::Path path;
      if (!property->IsSatisfied(&path)) {
        std::cout << std::endl;
        debug::PrintTraceDiff(
            path, [](const L1& l1, std::ostream& os) { os << l1; },
            [](const L1& l1, std::ostream& os) {
              os << "\e[1;32m================>\e[0m" << std::endl;
            },
            std::cout);

        std::cout << "\e[1;31m===> VERIFICATION FAILED (" << path.size()
                  << " steps): LIVELOCK\e[0m" << std::endl;
        std::cout << std::endl;
        return 1;
      }
    }
  }

  VLOG(0) << "DONE @ "
          << "visited states: " << eval->num_visited_states();
  return 0;
}

}  // namespace models

/* vim: set ts=2 sts=2 sw=2 et : */
