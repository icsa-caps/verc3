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

#include <cassert>
#include <cstddef>
#include <functional>
#include <future>
#include <sstream>
#include <stdexcept>
#include <vector>

#include <glog/logging.h>
#include <mc2lib/codegen/cats.hpp>
#include <mc2lib/memconsistency/cats.hpp>

#include "verc3/core/model_checking.hh"
#include "verc3/util.hh"

using namespace verc3;
namespace memc = mc2lib::memconsistency;

namespace {

class TSOMachineState {
 public:
  static constexpr mc2lib::types::WriteID kInitWrite =
      mc2lib::codegen::EvtStateCats::kInitWrite;
  static constexpr mc2lib::types::WriteID kMinWrite =
      mc2lib::codegen::EvtStateCats::kMinWrite;
  static constexpr mc2lib::types::WriteID kMaxWrite =
      mc2lib::codegen::EvtStateCats::kMaxWrite;
  static constexpr mc2lib::types::Poi kMinOther =
      mc2lib::codegen::EvtStateCats::kMinOther;
  static constexpr mc2lib::types::Poi kMaxOther =
      mc2lib::codegen::EvtStateCats::kMaxOther;

  struct Hash {
    auto operator()(const TSOMachineState& k) const {
      std::hash<std::size_t> hasher;
      auto h = hasher(42);

      for (const auto& v : k.global_) {
        CombineHash(v, &h);
      }

      for (const auto& t : k.threads_) {
        auto h_t = hasher(t.po_last_.type);
        CombineHash(t.po_last_.addr, &h_t);
        CombineHash(t.po_last_.iiid.poi, &h_t);

        for (const auto& v : t.local_buffer_) {
          CombineHash(v, &h_t);
        }

        // Commutative hash: symmetry reduction on threads.
        h += h_t;
      }

      CombineHash(k.next_read_id_, &h);
      CombineHash(k.next_write_id_, &h);
      return h;
    }
  };

  explicit TSOMachineState(mc2lib::types::Addr addrs,
                           mc2lib::types::Pid threads) {
    global_.resize(addrs, static_cast<mc2lib::types::WriteID>(kInitWrite));

    for (mc2lib::types::Pid i = 0; i < threads; ++i) {
      threads_.emplace_back(addrs);
    }
  }

  struct Thread {
    explicit Thread(mc2lib::types::Addr addrs) {
      po_last_ = memc::Event(memc::Event::kNone, -1, memc::Iiid(-1, -1));
      local_buffer_.resize(addrs,
                           static_cast<mc2lib::types::WriteID>(kInitWrite));
    }

    bool operator==(const Thread& rhs) const {
      return local_buffer_ == rhs.local_buffer_ && po_last_ == rhs.po_last_;
    }

    std::vector<mc2lib::types::WriteID> local_buffer_;
    memc::Event po_last_;
  };

  const memc::Event* GetWrite(mc2lib::types::Addr addr,
                              mc2lib::types::WriteID wid) {
    if (wid == kInitWrite) {
      return &ew_.events.Insert(
          memc::Event(memc::Event::kWrite, addr, memc::Iiid(-1, addr)));
    }

    assert(!writeid_to_event_.empty());
    auto evt = writeid_to_event_.find(wid);
    assert(evt != writeid_to_event_.end());
    return &evt->second;
  }

  const memc::Event* MakeEvent(mc2lib::types::Pid pid, mc2lib::types::Addr addr,
                               memc::Event::Type type) {
    const memc::Event* result = nullptr;

    if (type == memc::Event::kWrite) {
      result = &ew_.events.Insert(
          memc::Event(type, addr, memc::Iiid(pid, next_write_id_)), true);
      writeid_to_event_[next_write_id_] = *result;
      ++next_write_id_;

      CHECK(next_write_id_ < kMaxWrite) << "Writes exhausted";
    } else {
      result = &ew_.events.Insert(
          memc::Event(type, addr, memc::Iiid(pid, next_read_id_++)), true);

      CHECK(next_read_id_ < kMaxOther) << "Reads exhausted";
    }

    assert(result != nullptr);
    return result;
  }

  const void InsertPo(mc2lib::types::Pid pid, const memc::Event& evt) {
    assert(evt.type != memc::Event::kNone);

    if (threads_[pid].po_last_.type != memc::Event::kNone) {
      ew_.po.Insert(threads_[pid].po_last_, evt);
    }

    threads_[pid].po_last_ = evt;
  }

  bool operator==(const TSOMachineState& rhs) const {
    return threads_ == rhs.threads_ && global_ == rhs.global_;
  }

  bool Accept() const { return (ew_.rf.size() == 4 && ew_.co.size() == 4); }

  std::vector<Thread> threads_;
  std::vector<mc2lib::types::WriteID> global_;

  memc::cats::ExecWitness ew_;

 private:
  mc2lib::types::Poi next_read_id_ = kMinOther;
  mc2lib::types::WriteID next_write_id_ = kMinWrite;
  std::unordered_map<mc2lib::types::WriteID, memc::Event> writeid_to_event_;
};

class MemOp : public core::Rule<TSOMachineState> {
 public:
  using core::Rule<TSOMachineState>::Rule;

  bool PreCond(const TSOMachineState& state) const override {
    return Guard(state) && !state.Accept();
  }

  bool PostCond(const TSOMachineState& prev,
                const TSOMachineState& next) const override {
    return Guard(next);
  }

 private:
  bool Guard(const TSOMachineState& state) const {
    return !(state.ew_.rf.size() >= 5 || state.ew_.co.size() >= 5);
  }
};

class Read : public MemOp {
 public:
  explicit Read(mc2lib::types::Pid pid, mc2lib::types::Addr addr)
      : MemOp("Read"), pid_(pid), addr_(addr) {
    std::ostringstream oss;
    oss << pid_ << ":Read@" << addr_;
    name_ = oss.str();
  }

  bool Action(TSOMachineState* state) const override {
    auto obs_id = state->threads_[pid_].local_buffer_[addr_];

    auto evt_this = state->MakeEvent(pid_, addr_, memc::Event::kRead);
    auto evt_obs = state->GetWrite(addr_, obs_id);
    state->InsertPo(pid_, *evt_this);
    state->ew_.rf.Insert(*evt_obs, *evt_this);

    return true;
  }

 private:
  int pid_;
  int addr_;
};

class Write : public MemOp {
 public:
  explicit Write(mc2lib::types::Pid pid, mc2lib::types::Addr addr)
      : MemOp("Write"), pid_(pid), addr_(addr) {
    std::ostringstream oss;
    oss << pid_ << ":Write@" << addr_;
    name_ = oss.str();
  }

  bool Action(TSOMachineState* state) const override {
    auto obs_id = state->global_[addr_];

    auto evt_this = state->MakeEvent(pid_, addr_, memc::Event::kWrite);
    auto evt_obs = state->GetWrite(addr_, obs_id);
    state->InsertPo(pid_, *evt_this);
    state->ew_.co.Insert(*evt_obs, *evt_this);

    // The written value
    state->threads_[pid_].local_buffer_[addr_] = evt_this->iiid.poi;
    state->global_[addr_] = evt_this->iiid.poi;

    return true;
  }

 private:
  int pid_;
  int addr_;
};

class Propagate : public core::Rule<TSOMachineState> {
 public:
  explicit Propagate(mc2lib::types::Pid pid)
      : Rule<TSOMachineState>("Propagate"), pid_(pid) {
    std::ostringstream oss;
    oss << pid_ << ":Propagate";
    name_ = oss.str();
  }

  bool PreCond(const TSOMachineState& state) const override {
    return state.threads_[pid_].local_buffer_ != state.global_;
  }

  bool Action(TSOMachineState* state) const override {
    state->threads_[pid_].local_buffer_ = state->global_;
    // state->threads_[pid_].local_buffer_[0] = state->global_[0];  // <- bug
    return state;
  }

 private:
  int pid_;
};

class VerifyAxiomatic {
 public:
  void Async(core::StateQueue<TSOMachineState>* states) {
    Barrier();
    states_ = std::move(*states);
    assert(states->empty());
    future_ = std::async(std::launch::async, [this]() { (*this)(); });
  }

  void Barrier() {
    if (future_.valid()) {
      future_.get();  // will throw if there was an error
    }
  }

 private:
  void operator()() {
    std::size_t verified = 0;
    for (auto& s : states_) {
      VLOG_EVERY_N(0, 10000) << "Verifying against TSO ... "
                             << (verified * 100) / states_.size() << "%";

      s.ew_.co.set_props(memc::EventRel::kTransitiveClosure);
      s.ew_.po.set_props(memc::EventRel::kTransitiveClosure);

      memc::cats::Arch_TSO a_tso;
#if 0
      // Only worthwhile for large traces
      memc::cats::ArchProxy<memc::cats::Arch_TSO> prox(&a_tso);
      prox.Memoize(s.ew_);
      auto checker = prox.MakeChecker(&s.ew_);
#else
      auto checker = a_tso.MakeChecker(&a_tso, &s.ew_);
#endif
      checker->valid_exec();

      ++verified;
    }

    VLOG(0) << "Verifying against TSO ... 100% -- PASSED";
  }

 private:
  core::StateQueue<TSOMachineState> states_;
  std::future<void> future_;
};

}  // namespace

namespace models {

/**
 * Implements and model checks a bounded version of the TSO-LB model (bounded)
 * as discussed in the PhD thesis: M. Elver "Consistency Directed Cache
 * Coherence Protocols for Scalable Multiprocessors", 2016.
 */
int Main_tso_lb(int argc, char* argv[]) {
  core::TransitionSystem<TSOMachineState> tso(false);

  const mc2lib::types::Addr kMaxAddr = 2;
  const mc2lib::types::Pid kMaxPid = 4;

  for (mc2lib::types::Pid pid = 0; pid < kMaxPid; ++pid) {
    for (mc2lib::types::Addr addr = 0; addr < kMaxAddr; ++addr) {
      tso.Make<Read>(pid, addr);
      tso.Make<Write>(pid, addr);
    }

    tso.Make<Propagate>(pid);
  }

  std::size_t accept_states_total = 0;

  core::Eval_BFS_Hashing<core::TransitionSystem<TSOMachineState>> mc_bfs;
  VerifyAxiomatic verify_axiomatic;
  mc_bfs.set_monitor([&accept_states_total, &verify_axiomatic](
      const core::EvalBase<core::TransitionSystem<TSOMachineState>>& mc,
      core::StateQueue<TSOMachineState>* accept) {
    VLOG_EVERY_N(0, 1000) << "visited states: " << mc.num_visited_states()
                          << ", accept states: "
                          << (accept_states_total + accept->size())
                          << " | queued: " << mc.num_queued_states();

    if (accept->size() >= 50000) {
      accept_states_total += accept->size();
      verify_axiomatic.Async(accept);
    }

    return true;
  });

  try {
    auto result = mc_bfs.Evaluate({TSOMachineState(kMaxAddr, kMaxPid)}, &tso);
    accept_states_total += result.size();
    verify_axiomatic.Async(&result);
    verify_axiomatic.Barrier();
  } catch (const memc::Error& error) {
    LOG(ERROR) << "Memory consistency violation: " << error.what();
    return 1;
  } catch (const std::bad_alloc& e) {
    LOG(ERROR) << "Out of memory!";
    return 42;
  }

  VLOG(0) << "DONE @ "
          << "visited states: " << mc_bfs.num_visited_states()
          << ", accept states: " << accept_states_total;

  CHECK_EQ(662495ULL, accept_states_total)
      << "Unexpected number of accept states!";
  return 0;
}

}  // namespace models

/* vim: set ts=2 sts=2 sw=2 et : */
