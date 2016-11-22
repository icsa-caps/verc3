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

#include <algorithm>
#include <iostream>
#include <list>
#include <sstream>
#include <string>

#include "verc3/command.hh"
#include "verc3/core/ts.hh"
#include "verc3/core/types.hh"
#include "verc3/debug.hh"
#include "verc3/domain/protocol.hh"
#include "verc3/synthesis/solver.hh"
#include "verc3/util.hh"

// 1: MSI-small
// 2: MSI-large
#define SYNON 1
#define PRUNE true

using namespace verc3;

namespace {

// Constants {{{
constexpr std::size_t PROC_COUNT = 3;
constexpr std::size_t NET_MAX = PROC_COUNT + 1;
// }}}

// Type declarations {{{
typedef core::WeakUnion Node;

struct Msg {
  PRINTABLE_ENUM_CLASS(Type, friend, GetM, GetS, Inv, Put, Data, Ack);

  struct Hash {
    std::size_t operator()(const Msg& k) const {
      auto h = core::Hasher<decltype(k.mtype)>::type()(k.mtype);
      CombineHash(k.src.id_as<int>(), &h);
      return h;
    }
  };

  bool operator==(const Msg& rhs) const {
    return mtype == rhs.mtype && src == rhs.src;
  }

  friend std::ostream& operator<<(std::ostream& os, const Msg& msg) {
    os << "(" << msg.mtype << ", Node_" << msg.src.id_as<int>() << ")";
    return os;
  }

 public:
  Type mtype;
  Node src;
};

typedef core::ArraySet<Msg, NET_MAX> UnorderChan;

struct L1 {
  typedef core::ArraySet<L1, PROC_COUNT> ScalarSet;

  PRINTABLE_ENUM_CLASS(State, friend, I, M, S, I_M, M_I, I_S, IS_I);
  static constexpr std::size_t kStatesSize =
      static_cast<std::size_t>(State::IS_I) + 1;

  struct Hash {
    std::size_t operator()(const L1& k) const {
      auto h = core::Hasher<decltype(k.state)>::type()(k.state);
      CombineHash(k.chan, &h);
      return h;
    }
  };

  bool operator==(const L1& rhs) const {
    return state == rhs.state && chan == rhs.chan;
  }

  friend std::ostream& operator<<(std::ostream& os, const L1& l1) {
    os << " | state = " << l1.state << std::endl;

    os << " | chan = { ";
    l1.chan.for_each([&os](const Msg& msg) { os << msg << ", "; });
    os << "}" << std::endl;

    return os;
  }

 public:
  State state = State::I;
  UnorderChan chan;
};

struct Dir {
  PRINTABLE_ENUM_CLASS(State, friend, I, M, S, I_M, I_M_I, M_M, S_M, M_S);

  typedef core::ArraySet<Dir, 1, L1::ScalarSet> ScalarSet;

  struct Hash {
    std::size_t operator()(const Dir& k) const {
      auto h = core::Hasher<decltype(k.state)>::type()(k.state);
      CombineHash(k.owner, &h);
      CombineHash(k.chan, &h);
      CombineHash(k.sharers, &h);
      CombineHash(k.acks, &h);
      return h;
    }
  };

  bool operator==(const Dir& rhs) const {
    return state == rhs.state && owner == rhs.owner && chan == rhs.chan &&
           sharers == rhs.sharers && acks == rhs.acks;
  }

  friend std::ostream& operator<<(std::ostream& os, const Dir& dir) {
    os << " | state = " << dir.state << std::endl;
    os << " | owner = L1[Node_" << static_cast<std::size_t>(dir.owner) << "]"
       << std::endl;

    os << " | sharers = { ";
    dir.sharers.for_each([&os](L1::ScalarSet::ID id) {
      os << "L1[Node_" << static_cast<std::size_t>(id) << "], ";
    });
    os << "}" << std::endl;

    os << " | acks = " << dir.acks << std::endl;

    os << " | chan = { ";
    dir.chan.for_each([&os](const Msg& msg) { os << msg << ", "; });
    os << "}" << std::endl;

    return os;
  }

 public:
  State state = State::I;
  L1::ScalarSet::ID owner = L1::ScalarSet::ID::kUndefined;
  core::ArraySet<L1::ScalarSet::ID, PROC_COUNT> sharers;
  int acks = 0;
  UnorderChan chan;
};

// Construct complete machine's state
struct MachineState : core::StateNonAccepting {
  MachineState() : l1caches(true), dir(true) {}

  // Hashing and printing {{{
  struct Hash {
    std::size_t operator()(const MachineState& k) const {
      auto h = core::Hasher<decltype(k.l1caches)>::type()(k.l1caches);
      CombineHash(k.dir, &h);
      return h;
    }
  };

  bool operator==(const MachineState& rhs) const {
    return l1caches == rhs.l1caches && dir == rhs.dir;
  }

  friend std::ostream& operator<<(std::ostream& os, const MachineState& m) {
    m.l1caches.for_each_ID([&os, &m](L1::ScalarSet::ID id) {
      os << " +---< L1[Node_" << static_cast<std::size_t>(id) << "] >"
         << std::endl;
      os << *m.l1caches[id];
    });

    m.dir.for_each_ID([&os, &m](Dir::ScalarSet::ID id) {
      os << " +---< Dir[Node_" << static_cast<std::size_t>(id) << "] >"
         << std::endl;
      os << *m.dir[id];
    });

    return os;
  }
  // }}}

  // Helper functions {{{
  Dir::ScalarSet::ID Dir0() const { return dir.NextValid(); }

  void Send(Msg::Type mtype, Node dst, Node src) {
    UnorderChan* chan;

    if (l1caches.IsMember(dst.id_as<L1::ScalarSet::ID>())) {
      chan = &l1caches[dst.id_as<L1::ScalarSet::ID>()]->chan;
    } else {
      Expects(dir.IsMember(dst.id_as<Dir::ScalarSet::ID>()));
      chan = &dir[dst.id_as<Dir::ScalarSet::ID>()]->chan;
    }

    auto id = chan->NextInvalid();
    core::ErrorIf(id == UnorderChan::ID::kUndefined, "Too many messages");

    (*chan)[id]->mtype = mtype;
    (*chan)[id]->src = src;
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
        Send(Msg::Type::Inv, Node(id), src);
      }
    });
    dir()->sharers.Clear();
  }

  void ErrorUnhandledMsg(Node id) {
    std::ostringstream oss;
    oss << "Unhandled message type @ Node_" << id.id_as<int>() << " !";
    throw core::Error(oss.str());
  }
  // }}}

  bool DirectoryReceive(const Msg& msg);

  bool L1CacheReceive(L1::ScalarSet::ID id, const Msg& msg);

 public:
  L1::ScalarSet l1caches;
  Dir::ScalarSet dir;
};
// }}}

// Synthesis options {{{

/*
=============================
L1 options
=============================
*/

synthesis::LambdaOptions<void(Node, const Msg&, MachineState*)> L1_Response{
    "",
    {
        [](Node n, const Msg& m, MachineState* s) {
          // No response!
        },
        [](Node n, const Msg& m, MachineState* s) {
          s->Send(Msg::Type::Ack, Node(s->Dir0()), n);
        },
        [](Node n, const Msg& m, MachineState* s) {
          s->Send(Msg::Type::Put, m.src, n);
        },
    },
    PRUNE};

synthesis::LambdaOptions<void(L1*)> L1_State{
    "",
    {
        [](L1* s) { s->state = L1::State::I; },
        [](L1* s) { s->state = L1::State::M; },
        [](L1* s) { s->state = L1::State::S; },
        [](L1* s) { s->state = L1::State::I_M; },
        [](L1* s) { s->state = L1::State::M_I; },
        [](L1* s) { s->state = L1::State::I_S; },
        [](L1* s) { s->state = L1::State::IS_I; },
    },
    PRUNE};

/*
=============================
Dir options
=============================
*/

synthesis::LambdaOptions<void(const Msg&, MachineState*)> Dir_Response{
    "",
    {
        [](const Msg& m, MachineState* s) {
          // No response!
        },
        [](const Msg& m, MachineState* s) {
          s->Send(Msg::Type::Data, m.src, Node(s->Dir0()));
        },
        [](const Msg& m, MachineState* s) {
          core::ErrorIf(s->dir()->owner == L1::ScalarSet::ID::kUndefined, "");
          s->Send(Msg::Type::Data, Node(s->dir()->owner), Node(s->Dir0()));
        },
        [](const Msg& m, MachineState* s) {
          core::ErrorIf(s->dir()->owner == L1::ScalarSet::ID::kUndefined, "");
          s->Send(Msg::Type::Inv, Node(s->dir()->owner), Node(s->Dir0()));
        },
        [](const Msg& m, MachineState* s) {
          s->Send(Msg::Type::Ack, m.src, Node(s->Dir0()));
        },
        /*[](const Msg& m, MachineState* s) {
         s->BCastInv_ClearSharers(m.src);
        },*/
    },
    PRUNE};

synthesis::LambdaOptions<void(const Msg&, MachineState*)> Dir_Track{
    "",
    {
        [](const Msg& m, MachineState* s) {},
        [](const Msg& m, MachineState* s) {
          s->dir()->owner = m.src.id_as<L1::ScalarSet::ID>();
        },
        [](const Msg& m, MachineState* s) {
          s->dir()->owner = L1::ScalarSet::ID::kUndefined;
        },
    },
    PRUNE};

synthesis::LambdaOptions<void(Dir*)> Dir_State{
    "",
    {
        [](Dir* s) { s->state = Dir::State::I; },
        [](Dir* s) { s->state = Dir::State::M; },
        [](Dir* s) { s->state = Dir::State::S; },
        [](Dir* s) { s->state = Dir::State::I_M; },
        [](Dir* s) { s->state = Dir::State::I_M_I; },
        [](Dir* s) { s->state = Dir::State::M_M; },
        [](Dir* s) { s->state = Dir::State::S_M; },
    },
    PRUNE};

// }}}

// Directory message handler {{{

bool MachineState::DirectoryReceive(const Msg& msg) {
#define CASE_SYN(state, mtype)                                           \
  case Msg::Type::mtype:                                                 \
    SYNTHESIZE(Dir_Response, Dir_Response_##state##_##mtype, msg, this); \
    SYNTHESIZE(Dir_Track, Dir_Track_##state##_##mtype, msg, this);       \
    SYNTHESIZE(Dir_State, Dir_State_##state##_##mtype, dir());           \
    break;

  switch (dir()->state) {
    case Dir::State::I:
      switch (msg.mtype) {
        case Msg::Type::GetM:
          Send(Msg::Type::Data, msg.src, Node(Dir0()));
          dir()->owner = msg.src.id_as<L1::ScalarSet::ID>();
          dir()->state = Dir::State::I_M;
          break;

        case Msg::Type::GetS:
          Send(Msg::Type::Data, msg.src, Node(Dir0()));
          AddToSharersList(msg.src.id_as<L1::ScalarSet::ID>());
          dir()->state = Dir::State::S;
          break;

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
      break;

    case Dir::State::M:
      core::ErrorIf(dir()->owner == L1::ScalarSet::ID::kUndefined,
                    "Owner unset");
      core::ErrorIf(!dir()->sharers.empty(), "Sharers not empty");

      switch (msg.mtype) {
        case Msg::Type::GetM:
          Send(Msg::Type::Inv, Node(dir()->owner), Node(Dir0()));
          dir()->owner = msg.src.id_as<L1::ScalarSet::ID>();
          dir()->state = Dir::State::M_M;
          break;

        case Msg::Type::GetS:
          Send(Msg::Type::Inv, Node(dir()->owner), Node(Dir0()));
          dir()->owner = L1::ScalarSet::ID::kUndefined;
          AddToSharersList(msg.src.id_as<L1::ScalarSet::ID>());
          dir()->state = Dir::State::M_S;
          break;

        case Msg::Type::Put:
          Send(Msg::Type::Ack, msg.src, Node(Dir0()));
          dir()->owner = L1::ScalarSet::ID::kUndefined;
          dir()->state = Dir::State::I;
          break;

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
      break;

    case Dir::State::S:
      core::ErrorIf(dir()->owner != L1::ScalarSet::ID::kUndefined, "Owner set");

      switch (msg.mtype) {
        case Msg::Type::GetM:
          dir()->acks =
              dir()->sharers.size() -
              (dir()->sharers.Contains(msg.src.id_as<L1::ScalarSet::ID>()) ? 1
                                                                           : 0);
          if (dir()->acks) {
            BCastInv_ClearSharers(msg.src);
            dir()->state = Dir::State::S_M;
          } else {
            Send(Msg::Type::Data, msg.src, Node(Dir0()));
            dir()->sharers.Clear();
            dir()->state = Dir::State::I_M;
          }
          dir()->owner = msg.src.id_as<L1::ScalarSet::ID>();
          break;

        case Msg::Type::GetS:
          AddToSharersList(msg.src.id_as<L1::ScalarSet::ID>());
          Send(Msg::Type::Data, msg.src, Node(Dir0()));
          break;

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
      break;

    case Dir::State::I_M:
      switch (msg.mtype) {
        case Msg::Type::GetM:
        case Msg::Type::GetS:
          return false;

#if SYNON > 0
          CASE_SYN(I_M, Ack);
#else
        case Msg::Type::Ack:
          dir()->state = Dir::State::M;
          break;
#endif

#if SYNON > 2
          CASE_SYN(I_M, Put);
#else
        case Msg::Type::Put:
          Send(Msg::Type::Ack, msg.src, Node(Dir0()));
          dir()->owner = L1::ScalarSet::ID::kUndefined;
          dir()->state = Dir::State::I_M_I;
          break;
#endif

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
      break;

    case Dir::State::I_M_I:
      switch (msg.mtype) {
        case Msg::Type::GetM:
        case Msg::Type::GetS:
          return false;

#if SYNON > 2
          CASE_SYN(I_M_I, Ack);
#else
        case Msg::Type::Ack:
          dir()->state = Dir::State::I;
          break;
#endif

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
      break;

    case Dir::State::M_M:
      core::ErrorIf(dir()->owner == L1::ScalarSet::ID::kUndefined,
                    "Owner unset");

      switch (msg.mtype) {
        case Msg::Type::GetM:
        case Msg::Type::GetS:
          return false;

#if SYNON > 3
          CASE_SYN(M_M, Put);
#else
        case Msg::Type::Put:
          Send(Msg::Type::Data, Node(dir()->owner), Node(Dir0()));
          dir()->state = Dir::State::I_M;
          break;
#endif

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
      break;

    case Dir::State::S_M:
      core::ErrorIf(dir()->owner == L1::ScalarSet::ID::kUndefined,
                    "Owner unset");

      switch (msg.mtype) {
        case Msg::Type::GetM:
        case Msg::Type::GetS:
          return false;

        case Msg::Type::Ack:
          if (--dir()->acks == 0) {
            Send(Msg::Type::Data, Node(dir()->owner), Node(Dir0()));
            // SYNTHESIZE(Dir_State, Dir_State_S_M_Ack, dir());
            dir()->state = Dir::State::I_M;
          }
          break;

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
      break;

    case Dir::State::M_S:
      switch (msg.mtype) {
        case Msg::Type::GetM:
        case Msg::Type::GetS:
          return false;

#if SYNON > 0
          CASE_SYN(M_S, Put);
#else
        case Msg::Type::Put:
          Send(Msg::Type::Data, Node(*dir()->sharers()), Node(Dir0()));
          dir()->state = Dir::State::S;
          break;
#endif

        default:
          ErrorUnhandledMsg(Node(Dir0()));
      }
  }

#undef CASE_SYN
  return true;
}
// }}}

// L1 cache message handler {{{
bool MachineState::L1CacheReceive(L1::ScalarSet::ID id, const Msg& msg) {
  auto l1 = l1caches[id];
#define CASE_SYN(state, mtype)                                            \
  case Msg::Type::mtype:                                                  \
    SYNTHESIZE(L1_Response, L1_Response_##state##_##mtype, Node(id), msg, \
               this);                                                     \
    SYNTHESIZE(L1_State, L1_State_##state##_##mtype, l1);                 \
    break;

  switch (l1->state) {
    case L1::State::I:
      switch (msg.mtype) {
        case Msg::Type::Inv:
          Send(Msg::Type::Ack, Node(Dir0()), Node(id));
          break;

        default:
          ErrorUnhandledMsg(Node(id));
      }
      break;

    case L1::State::M:
      switch (msg.mtype) {
        case Msg::Type::Inv:
          Send(Msg::Type::Put, msg.src, Node(id));
          l1->state = L1::State::I;
          break;

        default:
          ErrorUnhandledMsg(Node(id));
      }
      break;

    case L1::State::S:
      switch (msg.mtype) {
        case Msg::Type::Inv:
          Send(Msg::Type::Ack, Node(Dir0()), Node(id));
          l1->state = L1::State::I;
          break;

        default:
          ErrorUnhandledMsg(Node(id));
      }
      break;

    case L1::State::I_M:
      switch (msg.mtype) {
#if SYNON > 1
        CASE_SYN(I_M, Data);
#else
        case Msg::Type::Data:
          Send(Msg::Type::Ack, Node(Dir0()), Node(id));
          l1->state = L1::State::M;
          break;
#endif

        case Msg::Type::Inv:
          Send(Msg::Type::Ack, Node(Dir0()), Node(id));
          break;

        default:
          ErrorUnhandledMsg(Node(id));
      }
      break;

    case L1::State::M_I:
      switch (msg.mtype) {
#if SYNON > 2
        CASE_SYN(M_I, Ack);
        CASE_SYN(M_I, Inv);
#else
        case Msg::Type::Ack:
        case Msg::Type::Inv:
          l1->state = L1::State::I;
          break;
#endif

        default:
          ErrorUnhandledMsg(Node(id));
      }
      break;

    case L1::State::I_S:
      switch (msg.mtype) {
#if SYNON > 3
        CASE_SYN(I_S, Data);
#else
        case Msg::Type::Data:
          l1->state = L1::State::S;
          break;
#endif

#if SYNON > 0
        CASE_SYN(I_S, Inv);
#else
        case Msg::Type::Inv:
          Send(Msg::Type::Ack, Node(Dir0()), Node(id));
          l1->state = L1::State::IS_I;
          break;
#endif

        default:
          ErrorUnhandledMsg(Node(id));
      }
      break;

    case L1::State::IS_I:
      switch (msg.mtype) {
#if SYNON > 1
        CASE_SYN(IS_I, Data);
#else
        case Msg::Type::Data:
          l1->state = L1::State::I;
          break;
#endif

#if SYNON > 2
        CASE_SYN(IS_I, Inv);
#else
        case Msg::Type::Inv:
          Send(Msg::Type::Ack, Node(Dir0()), Node(id));
          break;
#endif

        default:
          ErrorUnhandledMsg(Node(id));
      }
      break;
  }

  return true;
}
// }}}

// L1 rules {{{
class L1Action : public core::Rule<MachineState> {
 public:
  explicit L1Action(std::string name, L1::ScalarSet::ID id)
      : core::Rule<MachineState>(std::move(name)), id_(id) {
    std::ostringstream oss;
    oss << "L1[Node_" << static_cast<std::size_t>(id_) << "]:" << name_;
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
    state->l1caches[id()]->state = L1::State::I_S;
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
    state->l1caches[id()]->state = L1::State::I_M;
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
    state->l1caches[id()]->state = L1::State::I_M;
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
    state->l1caches[id()]->state = L1::State::I;
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
    state->Send(Msg::Type::Put, Node(state->Dir0()), Node(id()));
    state->l1caches[id()]->state = L1::State::M_I;
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
// }}}

// Directory rules {{{
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
// }}}

// TransitionSystem factory {{{

class AllStatesUsed : public core::Property<MachineState> {
 public:
  explicit AllStatesUsed() : core::Property<MachineState>("AllStatesUsed") {}

  typename core::Property<MachineState>::Ptr Clone() const override {
    return std::make_unique<AllStatesUsed>(*this);
  }

  void Reset() override { visited_states_.clear(); }

  bool Invariant(const MachineState& state) const override { return true; }

  void Next(const MachineState& state,
            const core::StateMap<MachineState>& next_states,
            const core::Unknowns& unknowns) override {
    for (const auto& kv : next_states) {
      kv.second.l1caches.for_each(
          [this](const L1& l1) { visited_states_.insert(l1.state); });
    }
  }

  bool IsSatisfied(bool verbose_on_error = true) const override {
    return visited_states_.size() == L1::kStatesSize;
  }

 protected:
  std::unordered_set<L1::State> visited_states_;
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
  });

  s.dir()->chan.for_each_ID(
      [&ts](UnorderChan::ID msg_id) { ts.Make<DirReceive>(msg_id); });

  // Invariants

  ts.Make<core::InvariantF<MachineState>>(
      "SWMR", [](const MachineState& state) {
        return state.l1caches.all_of([&state](const L1& c1) {
          return state.l1caches.all_of([&c1](const L1& c2) {
            if (&c1 != &c2 && c1.state == L1::State::M) {
              return c2.state != L1::State::M;
            }
            return true;
          });
        });
      });

  ts.Make<core::InvariantF<MachineState>>(
      "SaneState", [](const MachineState& state) {
        if (state.dir()->state == Dir::State::I) {
          return state.dir()->owner == L1::ScalarSet::ID::kUndefined;
        }
        return true;
      });

  ts.Make<protocol::LivelockFreedom<MachineState, L1::ScalarSet>>(
      [](const MachineState& state) -> const auto& { return state.l1caches; },
      [](const L1& s) {
        if (s.state == L1::State::I || s.state == L1::State::M ||
            s.state == L1::State::S)
          return true;

        // A real system should provide some fairness guarantees, otherwise a
        // core might livelock.
        bool has_inv = s.chan.any_of(
            [](const Msg& m) { return m.mtype == Msg::Type::Inv; });

        if ((s.state == L1::State::IS_I || s.state == L1::State::I_M) &&
            has_inv)
          return true;

        return false;
      });

  ts.Make<AllStatesUsed>();

  return ts;
}
// }}}

}  // namespace

using ErrorHashTrace = typename core::EvalBase<
    core::TransitionSystem<MachineState>>::ErrorHashTrace;

class Promising {
 public:
  Promising() : promising_visited_states_(0) {}

  template <class Solver>
  void Update(const Solver& solver, const synthesis::RangeEnumerate& r,
              const std::exception* e) {
    auto unk = dynamic_cast<const core::Unknown*>(e);
    auto eht = dynamic_cast<const ErrorHashTrace*>(e);

    // Record promising candidate's error trace if visited states larger than
    // last seen. Ignore those that failed due to unknowns.
    if (!unk &&
        solver.command().eval().num_visited_states() >
            promising_visited_states_) {
      std::lock_guard<std::mutex> mutex_lock(promising_mutex_);

      promising_visited_states_ = solver.command().eval().num_visited_states();
      promising_ = r;

      if (eht) {
        promising_eht_.reset(new ErrorHashTrace(*eht));
      } else {
        promising_eht_.reset();
      }

      promising_prop_.reset();
      for (const auto& prop : solver.transition_system().properties()) {
        if (!prop->IsSatisfied(false)) {
          promising_prop_ = prop->Clone();
          break;
        }
      }
    }
  }

  template <class State, class TransitionSystem>
  void Show(const State& initial_state, TransitionSystem&& ts) const {
    if (promising_eht_) {
      synthesis::t_range_enumerate = promising_;
      core::EvalBase<core::TransitionSystem<MachineState>> eval;
      auto trace = eval.MakeTraceFromHashTrace(
          {initial_state}, promising_eht_->hash_trace(), &ts);
      decltype(eval)::ErrorTrace et(promising_eht_->error(), trace);
      PrintErrorTrace<core::TransitionSystem<MachineState>>(
          et, promising_visited_states_);
    } else if (promising_prop_) {
      promising_prop_->IsSatisfied(true);
    }
    std::cout << promising_ << std::endl;
  }

 private:
  // Record most promising candidate if we fail and show trace.
  std::mutex promising_mutex_;
  synthesis::RangeEnumerate promising_;
  std::atomic<std::size_t> promising_visited_states_;
  std::unique_ptr<ErrorHashTrace> promising_eht_;
  typename core::Property<MachineState>::Ptr promising_prop_;
};

namespace models {

/**
 * This is the MSI protocol synthesis case study found in the VerC3 paper.
 */
int Main_synthesis_msi(int argc, char* argv[]) {
  MachineState initial_state;

  synthesis::ParallelSolver<core::TransitionSystem<MachineState>> solver;
  solver.set_num_threads(argc > 1 ? std::atoi(argv[1]) : 2);
  solver.set_pruning_enabled(PRUNE);

  Promising promising;

  solver.set_candidate_callback([&promising](
      const synthesis::Solver<core::TransitionSystem<MachineState>>& solver,
      const synthesis::RangeEnumerate& r,
      const std::exception* e) { promising.Update(solver, r, e); });

  auto result = solver({initial_state}, TransitionSystem);

  if (result.empty() && synthesis::g_range_enumerate.combinations() > 1) {
    // Show promising trace (if we have one).
    promising.Show(initial_state, TransitionSystem(initial_state));

    std::cout
        << "\e[1;31mSYNTHESIS FAILED! Please manually refine protocol.\e[0m"
        << std::endl;
    return 142;
  }

  InfoOut() << solver.pruning_patterns().size() << " patterns" << std::endl;
  return 0;
}

}  // namespace models

/* vim: set ts=2 sts=2 sw=2 et : */
