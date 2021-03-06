// WorkloadCharacterisation.cpp (Oclgrind)
// Copyright (c) 2017, Beau Johnston,
// The Australian National University. All rights reserved.
//
// This program is provided under a three-clause BSD license. For full
// license terms please see the LICENSE file distributed with this
// source code.

#include "core/common.h"
#include "core/Kernel.h"
#include "core/KernelInvocation.h"
#include "core/Memory.h"
#include "core/WorkGroup.h"
#include "core/WorkItem.h"
#include "WorkloadCharacterisation.h"

#include <algorithm>
#include <csignal>
#include <fstream>
#include <iomanip>
#include <math.h>
#include <numeric>
#include <sstream>

#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/raw_ostream.h"

using namespace oclgrind;
using namespace std;

#define COUNTED_LOAD_BASE (llvm::Instruction::OtherOpsEnd + 4)
#define COUNTED_STORE_BASE (COUNTED_LOAD_BASE + 8)
#define COUNTED_CALL_BASE (COUNTED_STORE_BASE + 8)

THREAD_LOCAL WorkloadCharacterisation::WorkerState
    WorkloadCharacterisation::m_state = {NULL};

WorkloadCharacterisation::WorkloadCharacterisation(const Context *context) : WorkloadCharacterisation::Plugin(context) {
  m_numberOfHostToDeviceCopiesBeforeKernelNamed = 0;
  m_last_kernel_name = "";
}

WorkloadCharacterisation::~WorkloadCharacterisation() {
  locale previousLocale = cout.getloc();
  locale defaultLocale("");
  cout.imbue(defaultLocale);

  // present memory transfer statistics -- only run once, since these are collected outside kernel invocations
  cout << "+-------------------------------------------------------------------------------------------------------+" << endl;
  cout << "|Memory Transfers -- statistics around host to device and device to host memory transfers               |" << endl;
  cout << "+=======================================================================================================+" << endl;
  // I can't imagine a scenario where data are copied from the device before a kernel is executed. So I use the deviceToHostCopy kernel names for the final statistics -- these names for the m_hostToDeviceCopy's are updated when the kernel is enqueued.
  std::vector<std::string> x = m_deviceToHostCopy;
  std::vector<std::string>::iterator unique_x = std::unique(x.begin(), x.end());
  x.resize(std::distance(x.begin(), unique_x));
  std::vector<std::string> unique_kernels_involved_with_device_to_host_copies = x;

  x = m_hostToDeviceCopy;
  unique_x = std::unique(x.begin(), x.end());
  x.resize(std::distance(x.begin(), unique_x));
  std::vector<std::string> unique_kernels_involved_with_host_to_device_copies = x;

  cout << "Total Host To Device Transfers (#) for kernel:" << endl;
  for (auto const &item : unique_kernels_involved_with_host_to_device_copies) {
    cout << "\t" << item << ": " << std::count(m_hostToDeviceCopy.begin(), m_hostToDeviceCopy.end(), item) << endl;
  }
  cout << "Total Device To Host Transfers (#) for kernel:" << endl;
  for (auto const &item : unique_kernels_involved_with_device_to_host_copies) {
    cout << "\t" << item << ": " << std::count(m_deviceToHostCopy.begin(), m_deviceToHostCopy.end(), item) << endl;
  }

  //write it out to special .csv file
  int logfile_count = 0;
  std::string logfile_name = "aiwc_memory_transfers_" + std::to_string(logfile_count) + ".csv";
  while (std::ifstream(logfile_name)) {
    logfile_count++;
    logfile_name = "aiwc_memory_transfers_" + std::to_string(logfile_count) + ".csv";
  }
  std::ofstream logfile;
  logfile.open(logfile_name);
  assert(logfile);
  logfile << "metric,kernel,count\n";

  for (auto const &item : unique_kernels_involved_with_host_to_device_copies) {
    logfile << "transfer: host to device," << item << "," << std::count(m_hostToDeviceCopy.begin(), m_hostToDeviceCopy.end(), item) << "\n";
  }
  for (auto const &item : unique_kernels_involved_with_device_to_host_copies) {
    logfile << "transfer: device to host," << item << "," << std::count(m_deviceToHostCopy.begin(), m_deviceToHostCopy.end(), item) << "\n";
  }
  logfile.close();

  // Restore locale
  cout.imbue(previousLocale);
}

void WorkloadCharacterisation::hostMemoryLoad(const Memory *memory, size_t address, size_t size) {
  //device to host copy -- synchronization
  m_deviceToHostCopy.push_back(m_last_kernel_name);
}

void WorkloadCharacterisation::hostMemoryStore(const Memory *memory, size_t address, size_t size, const uint8_t *storeData) {
  //host to device copy -- synchronization
  m_hostToDeviceCopy.push_back(m_last_kernel_name);
  m_numberOfHostToDeviceCopiesBeforeKernelNamed++;
}

void WorkloadCharacterisation::threadMemoryLedger(size_t address, uint32_t timestep, Size3 localID) {
  WorkloadCharacterisation::ledgerElement le;
  le.address = address;
  le.timestep = timestep;
  (m_state.ledger)//[groupID.x * m_group_num.x + groupID.y * m_group_num.y
        // + groupID.z * m_group_num.z]
        [localID.x * m_local_num.y * m_local_num.z + localID.y * m_local_num.z
         + localID.z].push_back(le);
}

void WorkloadCharacterisation::memoryLoad(const Memory *memory, const WorkItem *workItem, size_t address, size_t size) {
  if (memory->getAddressSpace() != AddrSpacePrivate) {
    //(*m_state.memoryOps)[pair(address, true)]++;
    (*m_state.loadOps)[address]++;
    threadMemoryLedger(address, 0, workItem->getLocalID());
  }
}

void WorkloadCharacterisation::memoryStore(const Memory *memory, const WorkItem *workItem, size_t address, size_t size, const uint8_t *storeData) {
  if (memory->getAddressSpace() != AddrSpacePrivate) {
    //(*m_state.memoryOps)[pair(address, false)]++;
    (*m_state.storeOps)[address]++;
    threadMemoryLedger(address, 0, workItem->getLocalID());
  }
}

void WorkloadCharacterisation::memoryAtomicLoad(const Memory *memory, const WorkItem *workItem, AtomicOp op, size_t address, size_t size) {
  if (memory->getAddressSpace() != 0) {
    //(*m_state.memoryOps)[pair(address, true)]++;
    (*m_state.loadOps)[address]++;
    threadMemoryLedger(address, 0, workItem->getLocalID());
  }
}

void WorkloadCharacterisation::memoryAtomicStore(const Memory *memory, const WorkItem *workItem, AtomicOp op, size_t address, size_t size) {
  if (memory->getAddressSpace() != 0) {
    //(*m_state.memoryOps)[pair(address, false)]++;
    (*m_state.storeOps)[address]++;
    threadMemoryLedger(address, 0, workItem->getLocalID());
  }
}

void WorkloadCharacterisation::instructionExecuted(
    const WorkItem *workItem, const llvm::Instruction *instruction,
    const TypedValue &result) {

  unsigned opcode = instruction->getOpcode();
  std::string opcode_name = llvm::Instruction::getOpcodeName(opcode);
  (*m_state.computeOps)[opcode_name]++;

  bool isMemoryInst = false;
  unsigned addressSpace;

  //get all unique labels -- for register use -- and the # of instructions between loads and stores -- as the freedom to reorder
  m_state.ops_between_load_or_store++;
  if (auto inst = llvm::dyn_cast<llvm::LoadInst>(instruction)) {
    isMemoryInst = true;
    addressSpace = inst->getPointerAddressSpace();
    std::string name = inst->getPointerOperand()->getName().data();
    (*m_state.loadInstructionLabels)[name]++;
    m_state.instructionsBetweenLoadOrStore->push_back(m_state.ops_between_load_or_store);
    m_state.ops_between_load_or_store = 0;
  } else if (auto inst = llvm::dyn_cast<llvm::StoreInst>(instruction)) {
    isMemoryInst = true;
    addressSpace = inst->getPointerAddressSpace();
    std::string name = inst->getPointerOperand()->getName().data();
    (*m_state.storeInstructionLabels)[name]++;
    m_state.instructionsBetweenLoadOrStore->push_back(m_state.ops_between_load_or_store);
    m_state.ops_between_load_or_store = 0;
  }
  if (isMemoryInst) {
    switch (addressSpace) {
    case AddrSpaceLocal: {
      m_state.local_memory_access_count++;
      break;
    }
    case AddrSpaceGlobal: {
      m_state.global_memory_access_count++;
      break;
    }
    case AddrSpaceConstant: {
      m_state.constant_memory_access_count++;
      break;
    }
    case AddrSpacePrivate:
    default: {
      // we don't count these
    }
    }
  }

  //collect conditional branches and the associated trace to count which ones were taken and which weren't
  if (m_state.previous_instruction_is_branch == true) {
    std::string Str;
    llvm::raw_string_ostream OS(Str);
    instruction->getParent()->printAsOperand(OS, false);
    OS.flush();
    if (Str == m_state.target1)
      (*m_state.branchOps)[m_state.branch_loc].push_back(true); //taken
    else if (Str == m_state.target2) {
      (*m_state.branchOps)[m_state.branch_loc].push_back(false); //not taken
    } else {
      cout << "Error in branching!" << endl;
      cout << "Str was: " << Str << " but target was either: " << m_state.target1 << " or: " << m_state.target2 << endl;
      std::raise(SIGINT);
    }
    m_state.previous_instruction_is_branch = false;
  }
  //if a conditional branch -- they have labels and thus temporarily store them and see where we jump to in the next instruction
  if (opcode == llvm::Instruction::Br && instruction->getNumOperands() == 3) {
    if (instruction->getOperand(1)->getType()->isLabelTy() && instruction->getOperand(2)->getType()->isLabelTy()) {
      m_state.previous_instruction_is_branch = true;
      std::string Str;
      llvm::raw_string_ostream OS(Str);
      instruction->getOperand(1)->printAsOperand(OS, false);
      OS.flush();
      m_state.target1 = Str;
      Str = "";
      instruction->getOperand(2)->printAsOperand(OS, false);
      OS.flush();
      m_state.target2 = Str;
      llvm::DebugLoc loc = instruction->getDebugLoc();
      m_state.branch_loc = loc.getLine();
    }
  }

  //counter for instructions to barrier and other parallelism metrics
  m_state.instruction_count++;
  m_state.workitem_instruction_count++;

  //SIMD instruction width metrics use the following
  (*m_state.instructionWidth)[result.num]++;

  //TODO: add support for Phi, Switch and Select control operations
}

void WorkloadCharacterisation::workItemBarrier(const WorkItem *workItem) {
  m_state.barriers_hit++;
  m_state.instructionsBetweenBarriers->push_back(m_state.instruction_count);
  m_state.instruction_count = 0;
}

vector<double> parallelSpatialLocality(vector < vector < WorkloadCharacterisation::ledgerElement> > hist);

void WorkloadCharacterisation::workGroupBarrier(const WorkGroup *workGroup, uint32_t flags) {
  vector<double> psl = parallelSpatialLocality(m_state.ledger);
  size_t maxLength = 0;
  for (size_t i = 0; i < m_state.ledger.size(); i++) {
    maxLength = m_state.ledger[i].size() > maxLength ? m_state.ledger[i].size() : maxLength;
    m_state.ledger[i].clear();
  }
  m_state.psl_per_barrier->push_back(std::make_pair(psl, maxLength));
}

void WorkloadCharacterisation::workItemClearBarrier(const WorkItem *workItem) {
  m_state.instruction_count = 0;
}

void WorkloadCharacterisation::workItemBegin(const WorkItem *workItem) {
  m_state.threads_invoked++;
  m_state.instruction_count = 0;
  m_state.workitem_instruction_count = 0;
  m_state.ops_between_load_or_store = 0;
  //Size_3 local_ID = workItem->getLocalID;
  //m_state.work_item_no = localID.x * m_local_num.y * m_local_num.z + localID.y * m_local_num.z
  //       + localID.z;
  //m_state.work_group_no = 0;
}

void WorkloadCharacterisation::workItemComplete(const WorkItem *workItem) {
  m_state.instructionsBetweenBarriers->push_back(m_state.instruction_count);
  m_state.instructionsPerWorkitem->push_back(m_state.workitem_instruction_count);
}

void WorkloadCharacterisation::kernelBegin(const KernelInvocation *kernelInvocation) {
  //update the list of memory copies from host to device; since the only reason to write to the device is before an execution.
  m_last_kernel_name = kernelInvocation->getKernel()->getName();

  int end_of_list = m_hostToDeviceCopy.size() - 1;
  for (int i = 0; i < m_numberOfHostToDeviceCopiesBeforeKernelNamed; i++) {
    m_hostToDeviceCopy[end_of_list - i] = m_last_kernel_name;
  }
  m_numberOfHostToDeviceCopiesBeforeKernelNamed = 0;

  //m_memoryOps.clear();
  m_storeOps.clear();
  m_loadOps.clear();
  m_computeOps.clear();
  m_branchPatterns.clear();
  m_branchCounts.clear();
  m_instructionsToBarrier.clear();
  m_instructionWidth.clear();
  m_instructionsPerWorkitem.clear();
  m_instructionsBetweenLoadOrStore.clear();
  m_loadInstructionLabels.clear();
  m_storeInstructionLabels.clear();
  m_threads_invoked = 0;
  m_barriers_hit = 0;
  m_global_memory_access = 0;
  m_local_memory_access = 0;
  m_constant_memory_access = 0;

  m_group_num = kernelInvocation->getNumGroups();
  m_local_num = kernelInvocation->getLocalSize();
  m_psl_per_group = vector<vector<double>>();
}


vector<double> entropy(unordered_map<size_t, uint32_t> histogram) {
  std::vector<std::unordered_map<size_t, uint32_t>> local_address_count(11, unordered_map<size_t, uint32_t>());
  local_address_count[0] = histogram;
  uint64_t total_access_count = 0;
  vector<double> loc_entropy = vector<double>(11);

  for (const auto &m : histogram) {
    for (int nskip = 1; nskip <= 10; nskip++) {
      size_t local_addr = m.first >> nskip;
      local_address_count[nskip][local_addr] += m.second;
    }
    total_access_count += m.second;
  }

  if (total_access_count == 0) {
    loc_entropy = vector<double>(11, 0.0);
    return loc_entropy;
  }

  for (int nskip = 0; nskip < 11; nskip++) {
    double local_entropy = 0.0;
    for (const auto &it : local_address_count[nskip]) {
      double prob = (double)(it.second) * 1.0 / (double)(total_access_count+1);
      local_entropy = local_entropy - prob * std::log2(prob);
    }
    loc_entropy[nskip] = (float)local_entropy;
  }

  return loc_entropy;
}

vector<double> parallelSpatialLocality(vector < vector < WorkloadCharacterisation::ledgerElement> > hist) {
  size_t maxLength = 0;
  for (size_t i = 0; i < hist.size(); i++)
    maxLength = hist[i].size() > maxLength ? hist[i].size() : maxLength;

  unordered_map <size_t, uint32_t> histogram;
  vector<vector<double>> entropies = vector<vector<double>>(maxLength);

  for (size_t i = 0; i < maxLength; i++) { // for each timestep
    histogram.clear();
    for (size_t j = 0; j < hist.size(); j++) {
      if (i >= hist[j].size())
        continue;
      WorkloadCharacterisation::ledgerElement current = hist[j][i];
      histogram[current.address] = histogram[current.address] + 1;
    }

    entropies[i] = entropy(histogram);
  }

  vector<double> psl = vector<double>(11, 0.0);
  for (uint32_t i = 0; i < 11; i++) {
    for (size_t j = 0; j < entropies.size(); j++)
      psl[i] += entropies[j][i];
    psl[i] = psl[i] * 1.0/ ((double)entropies.size() + 1);
  }
  return psl;
}

void WorkloadCharacterisation::kernelEnd(const KernelInvocation *kernelInvocation) {
  std::string logfile_name;

  const char *result_path = getenv("OCLGRIND_WORKLOAD_CHARACTERISATION_OUTPUT_PATH");
  if (result_path != NULL) {
    logfile_name = std::string(result_path);
  } else {
    int logfile_count = 0;
    logfile_name = "aiwc_" + kernelInvocation->getKernel()->getName() + "_" + std::to_string(logfile_count) + ".csv";
    while (std::ifstream(logfile_name)) {
      logfile_count++;
      logfile_name = "aiwc_" + kernelInvocation->getKernel()->getName() + "_" + std::to_string(logfile_count) + ".csv";
    }
  }
  std::ofstream logfile;
  logfile.open(logfile_name);
  assert(logfile);

  std::vector<std::pair<std::string, size_t>> sorted_ops(m_computeOps.size());
  std::partial_sort_copy(m_computeOps.begin(), m_computeOps.end(), sorted_ops.begin(), sorted_ops.end(), [](const std::pair<std::string, size_t> &left, const std::pair<std::string, size_t> &right) {
    return (left.second > right.second);
  });

  logfile << "opcode_count, ";
  for (auto const &item : sorted_ops) {
    logfile << item.first << " " << item.second << ";";
  }
  logfile << std::endl;

  size_t operation_count = 0;
  for (auto const &item : sorted_ops) {
    operation_count += item.second;
  }

  double freedom_to_reorder = std::accumulate(m_instructionsBetweenLoadOrStore.begin(), m_instructionsBetweenLoadOrStore.end(), 0.0);
  freedom_to_reorder = freedom_to_reorder / m_instructionsBetweenLoadOrStore.size();

  logfile << "freedom_to_reorder, " << freedom_to_reorder << std::endl;

  double resource_pressure = 0;
  for (auto const &item : m_storeInstructionLabels) {
    resource_pressure += item.second;
  }

  for (auto const &item : m_loadInstructionLabels) {
    resource_pressure += item.second;
  }

  resource_pressure = resource_pressure / m_threads_invoked;
  logfile << "resource_pressure, " << resource_pressure << std::endl;
  logfile << "work_items, " << m_threads_invoked << std::endl;
  logfile << "total_barriers_hit, " << m_barriers_hit << std::endl;

  uint32_t itb_min = *std::min_element(m_instructionsToBarrier.begin(), m_instructionsToBarrier.end());
  uint32_t itb_max = *std::max_element(m_instructionsToBarrier.begin(), m_instructionsToBarrier.end());

  double itb_median;
  std::vector<uint32_t> itb = m_instructionsToBarrier;
  sort(itb.begin(), itb.end());

  size_t size = itb.size();
  if (size % 2 == 0) {
    itb_median = (itb[size / 2 - 1] + itb[size / 2]) / 2;
  } else {
    itb_median = itb[size / 2];
  }

  logfile << "instructions_to_barrier_min, " << itb_min << std::endl;
  logfile << "instructions_to_barrier_max, " << itb_max << std::endl;
  logfile << "instructions_to_barrier_median, " << itb_median << std::endl;

  uint32_t ipt_min = *std::min_element(m_instructionsPerWorkitem.begin(), m_instructionsPerWorkitem.end());
  uint32_t ipt_max = *std::max_element(m_instructionsPerWorkitem.begin(), m_instructionsPerWorkitem.end());

  uint32_t ipt_median;
  std::vector<uint32_t> ipt = m_instructionsPerWorkitem;
  sort(ipt.begin(), ipt.end());

  size = ipt.size();
  if (size % 2 == 0) {
    ipt_median = (ipt[size / 2 - 1] + ipt[size / 2]) / 2;
  } else {
    ipt_median = ipt[size / 2];
  }

  logfile << "instructions_per_thread_min, " << ipt_min << std::endl;
  logfile << "instructions_per_thread_max, " << ipt_max << std::endl;
  logfile << "instructions_per_thread_median, " << ipt_median << std::endl;

  using pair_type = decltype(m_instructionWidth)::value_type;

  uint16_t simd_min = std::min_element(m_instructionWidth.begin(), m_instructionWidth.end(), [](const pair_type &a, const pair_type &b) { return a.first < b.first; })->first;
  uint16_t simd_max = std::max_element(m_instructionWidth.begin(), m_instructionWidth.end(), [](const pair_type &a, const pair_type &b) { return a.first < b.first; })->first;

  uint32_t simd_sum = 0;
  uint32_t simd_num = 0;
  for (const auto &item : m_instructionWidth) {
    simd_sum += item.second * item.first;
    simd_num += item.second;
  }
  double simd_mean = simd_sum / (double)simd_num;
  std::vector<double> diff(m_instructionWidth.size());
  std::transform(m_instructionWidth.begin(), m_instructionWidth.end(), diff.begin(), [simd_mean](const pair_type &x) { return (x.first - simd_mean) * (x.first - simd_mean) * x.second; });
  double simd_sq_sum = std::accumulate(diff.begin(), diff.end(), 0.0);
  double simd_stdev = std::sqrt(simd_sq_sum / (double)simd_num);

  logfile << "simd_sum, " << simd_sum << std::endl;
  logfile << "simd_num, " << simd_num << std::endl;
  logfile << "simd_width_min, " << simd_min << std::endl;
  logfile << "simd_width_max, " << simd_max << std::endl;
  logfile << "simd_width_mean, " << simd_mean << std::endl;
  logfile << "simd_width_stdev, " << simd_stdev << std::endl;

  std::vector<std::unordered_map<size_t, uint32_t>> local_address_count(11);

  size_t load_count = 0;
  size_t store_count = 0;

  for (const auto &m : m_storeOps) {
    for (int nskip = 0; nskip <= 10; nskip++) {
      size_t local_addr = m.first >> nskip;
      local_address_count[nskip][local_addr] += m.second;
    }
    store_count += m.second;
  }

  for (const auto &m : m_loadOps) {
    for (int nskip = 0; nskip <= 10; nskip++) {
      size_t local_addr = m.first >> nskip;
      local_address_count[nskip][local_addr] += m.second;
    }
    load_count += m.second;
  }

  std::vector<std::pair<size_t, uint32_t>> sorted_count(local_address_count[0].size());
  std::partial_sort_copy(local_address_count[0].begin(), local_address_count[0].end(), sorted_count.begin(), sorted_count.end(), [](const std::pair<size_t, uint32_t> &left, const std::pair<size_t, uint32_t> &right) {
    return (left.second > right.second);
  });

  size_t memory_access_count = 0;
  for (auto const &e : sorted_count) {
    memory_access_count += e.second;
  }

  logfile << "memory_access_count, " << memory_access_count << std::endl;
  logfile << "unique_memory_addresses_accessed, " << local_address_count[0].size() << std::endl;
  logfile << "unique_memory_addresses_read, " << m_loadOps.size() << std::endl;
  logfile << "unique_memory_addresses_written, " << m_storeOps.size() << std::endl;
  logfile << "total_memory_reads, " << load_count << std::endl;
  logfile << "total_memory_writes, " << store_count << std::endl;

  size_t significant_memory_access_count = (size_t)ceil(memory_access_count * 0.9);
  size_t unique_memory_addresses = 0;
  size_t access_count = 0;
  while (access_count < significant_memory_access_count) {
    access_count += sorted_count[unique_memory_addresses].second;
    unique_memory_addresses++;
  }

  logfile << "sig_unique_memory_addresses, " << unique_memory_addresses << std::endl;

  double mem_entropy = 0.0;
  for (const auto &it : sorted_count) {
    uint32_t value = it.second;
    double prob = (double)value * 1.0 / (double)memory_access_count;
    mem_entropy = mem_entropy - prob * std::log2(prob);
  }

  logfile << "global_memory_address_entropy, " << mem_entropy << std::endl;
  logfile << "lsb_skipped_entropy, ";

  std::vector<float> loc_entropy;
  for (int nskip = 1; nskip < 11; nskip++) {
    double local_entropy = 0.0;
    for (const auto &it : local_address_count[nskip]) {
      double prob = (double)(it.second) * 1.0 / (double)memory_access_count;
      local_entropy = local_entropy - prob * std::log2(prob);
    }
    loc_entropy.push_back((float)local_entropy);
    logfile << nskip << " " << local_entropy << ";";
  }
  logfile << std::endl;

  logfile << "lsb_skipped_npsl, ";

  vector<double> avg_psl = vector<double>();
  double avg_psl_sum = 0.0;
  uint32_t items_per_group = (m_local_num[0] * m_local_num[1] * m_local_num[2]);
  for (size_t i = 0; i < m_psl_per_group[0].size(); i++) {
    double avg_psl_i = 0.0;
    for (size_t j = 0; j < m_psl_per_group.size(); j++){
      avg_psl_i += m_psl_per_group[j][i];
    avg_psl_i = (avg_psl_i / double(m_psl_per_group.size())) / std::log2(double(items_per_group + 1));
    }
    avg_psl.push_back(avg_psl_i);
    avg_psl_sum += avg_psl_i;
    logfile << i << " " << avg_psl_i << ";";
  }
  logfile << std::endl;

  logfile << "num_global_memory_access, " << m_global_memory_access << std::endl;
  logfile << "num_local_memory_access, " << m_local_memory_access << std::endl;
  logfile << "num_constant_memory_access, " << m_constant_memory_access << std::endl;

  std::vector<std::pair<size_t, uint32_t>> sorted_branch_ops(m_branchCounts.size());
  std::partial_sort_copy(m_branchCounts.begin(), m_branchCounts.end(), sorted_branch_ops.begin(), sorted_branch_ops.end(), [](const std::pair<size_t, uint32_t> &left, const std::pair<size_t, uint32_t> &right) {
    return (left.second > right.second);
  });

  logfile << "branch_at_line_count, ";

  size_t branch_op_count = 0;
  for (auto const &x : sorted_branch_ops) {
    branch_op_count += x.second;
    logfile << x.first << " " << x.second << ";";
  }
  logfile << std::endl;

  size_t significant_branch_op_count = (size_t)ceil(branch_op_count * 0.9);

  size_t unique_branch_addresses = 0;
  size_t branch_count = 0;
  while (branch_count < significant_branch_op_count) {
    branch_count += sorted_branch_ops[unique_branch_addresses].second;
    unique_branch_addresses++;
  }

  //generate a history pattern
  //(arbitarily selected to a history of m=16 branches?)
  const unsigned m = 16;
  float average_entropy = 0.0f;
  float yokota_entropy = 0.0f;
  float yokota_entropy_per_workload = 0.0f;
  unsigned N = 0;

  for (auto const &branch : m_branchPatterns) {
    for (auto const &h : branch.second) {
      uint16_t pattern = h.first;
      uint32_t number_of_occurrences = h.second;
      //for each history pattern compute the probability of the taken branch
      unsigned taken = 0;
      uint16_t p = pattern;
      // count taken branches using Kernighan's algorithm
      while (p) {
        p &= p - 1;
        taken++;
      }
      unsigned not_taken = m - taken;
      float probability_of_taken = (float)taken / (float)(not_taken + taken);

      //compute Yokota branch entropy
      if (probability_of_taken != 0) {
        yokota_entropy -= number_of_occurrences * probability_of_taken * std::log2(probability_of_taken);
        yokota_entropy_per_workload -= probability_of_taken * std::log2(probability_of_taken);
      }
      //compute linear branch entropy
      float linear_branch_entropy = 2 * std::min(probability_of_taken, 1 - probability_of_taken);
      average_entropy += number_of_occurrences * linear_branch_entropy;
      N += number_of_occurrences;
    }
  }
  average_entropy = average_entropy / N;
  if (isnan(average_entropy)) {
    average_entropy = 0.0;
  }

  logfile << "branch_history, " << m << std::endl;
  logfile << "yokota_entropy, " << yokota_entropy << std::endl;
  logfile << "yokota_entropy_per_workload, " << yokota_entropy_per_workload << std::endl;
  logfile << "average_entropy, " << average_entropy << std::endl;

  logfile.close();

  cout << endl
       << "The Architecture-Independent Workload Characterisation was written to file: " << logfile_name << endl;

  // Reset kernel counts, ready to start anew
  //m_memoryOps.clear();
  m_loadOps.clear();
  m_storeOps.clear();
  m_computeOps.clear();
  m_branchPatterns.clear();
  m_branchCounts.clear();
  m_instructionsToBarrier.clear();
  m_instructionsPerWorkitem.clear();
  m_threads_invoked = 0;
  m_instructionsBetweenLoadOrStore.clear();
  m_loadInstructionLabels.clear();
  m_storeInstructionLabels.clear();
}

void WorkloadCharacterisation::workGroupBegin(const WorkGroup *workGroup) {
  // Create worker state if haven't already
  //if (!m_state.memoryOps) {
  if (!m_state.storeOps) {
    //m_state.memoryOps = new unordered_map<pair<size_t, bool>, uint32_t>;
    m_state.storeOps = new unordered_map<size_t, uint32_t>;
    m_state.loadOps = new unordered_map<size_t, uint32_t>;
    m_state.computeOps = new unordered_map<std::string, size_t>;
    m_state.branchOps = new unordered_map<size_t, std::vector<bool>>;
    m_state.instructionsBetweenBarriers = new vector<uint32_t>;
    m_state.instructionWidth = new unordered_map<uint16_t, size_t>;
    m_state.instructionsPerWorkitem = new vector<uint32_t>;
    m_state.instructionsBetweenLoadOrStore = new vector<uint32_t>;
    m_state.loadInstructionLabels = new unordered_map<std::string, size_t>;
    m_state.storeInstructionLabels = new unordered_map<std::string, size_t>;
    m_state.ledger = vector<vector<WorkloadCharacterisation::ledgerElement>>
      (m_local_num.x * m_local_num.y * m_local_num.z, vector<ledgerElement>());
    m_state.psl_per_barrier = new vector<pair<vector<double>,uint64_t>>;
  }

  //m_state.memoryOps->clear();
  m_state.storeOps->clear();
  m_state.loadOps->clear();
  m_state.computeOps->clear();
  m_state.branchOps->clear();
  m_state.instructionsBetweenBarriers->clear();
  m_state.instructionWidth->clear();
  m_state.instructionsPerWorkitem->clear();
  m_state.instructionsBetweenLoadOrStore->clear();
  m_state.loadInstructionLabels->clear();
  m_state.storeInstructionLabels->clear();

  m_state.threads_invoked = 0;
  m_state.instruction_count = 0;
  m_state.barriers_hit = 0;

  //memory type access count variables
  m_state.constant_memory_access_count = 0;
  m_state.local_memory_access_count = 0;
  m_state.global_memory_access_count = 0;

  //branch logic variables
  m_state.previous_instruction_is_branch = false;
  m_state.target1 = "";
  m_state.target2 = "";
  m_state.branch_loc = 0;

  for (size_t i = 0; i < (m_state.ledger).size(); i++)
    (m_state.ledger)[i].clear();
}

void WorkloadCharacterisation::workGroupComplete(const WorkGroup *workGroup) {

  lock_guard<mutex> lock(m_mtx);
  // merge operation counts back into global unordered map
  for (auto const &item : (*m_state.computeOps))
    m_computeOps[item.first] += item.second;

  // merge memory operations into global list
  // for (auto const &item : (*m_state.memoryOps))
  //   m_memoryOps[item.first] += item.second;

  for (auto const &item : (*m_state.storeOps))
    m_storeOps[item.first] += item.second;

  for (auto const &item : (*m_state.loadOps))
    m_loadOps[item.first] += item.second;

  // merge control operations into global unordered maps
  const unsigned m = 16;
  for (auto const &branch : (*m_state.branchOps)) {
    m_branchCounts[branch.first] += branch.second.size();

    // compute branch patterns
    //if we have fewer branches than the history window, skip it.
    if (branch.second.size() < m)
      continue;

    // generate the set of history patterns - one bit per branch encounter
    std::unordered_map<uint16_t, uint32_t> H;
    uint16_t current_pattern = 0;
    for (unsigned i = 0; i < branch.second.size(); i++) {
      bool b = branch.second[i];
      current_pattern = (current_pattern << 1) | (b ? 1 : 0);
      if (i >= m - 1) {
        // we now have m bits of pattern to compare
        m_branchPatterns[branch.first][current_pattern]++;
      }
    }
  }

  // add the current work-group item / thread counter to the global variable
  m_threads_invoked += m_state.threads_invoked;

  // add the instructions between barriers back to the global setting
  for (auto const &item : (*m_state.instructionsBetweenBarriers))
    m_instructionsToBarrier.push_back(item);

  m_barriers_hit += m_state.barriers_hit;

  // add the SIMD scores back to the global setting
  for (auto const &item : (*m_state.instructionWidth))
    m_instructionWidth[item.first] += item.second;

  // add the instructions executed per workitem scores back to the global setting
  for (auto const &item : (*m_state.instructionsPerWorkitem))
    m_instructionsPerWorkitem.push_back(item);

  // add the instruction reordering (flexability) metrics
  for (auto const &item : (*m_state.instructionsBetweenLoadOrStore))
    m_instructionsBetweenLoadOrStore.push_back(item);

  for (auto const &item : (*m_state.loadInstructionLabels))
    m_loadInstructionLabels[item.first] += item.second;

  for (auto const &item : (*m_state.storeInstructionLabels))
    m_storeInstructionLabels[item.first] += item.second;

  // merge memory type access count variables
  m_constant_memory_access += m_state.constant_memory_access_count;
  m_local_memory_access += m_state.local_memory_access_count;
  m_global_memory_access += m_state.global_memory_access_count;

  vector<double> psl = parallelSpatialLocality(m_state.ledger);
  size_t maxLength = 0;
  for (size_t i = 0; i < m_state.ledger.size(); i++) {
    maxLength = m_state.ledger[i].size() > maxLength ? m_state.ledger[i].size() : maxLength;
    m_state.ledger[i].clear();
  }

  m_state.psl_per_barrier->push_back(std::make_pair(psl, maxLength));

  maxLength = 0;
  vector<double> weighted_avg_psl = vector<double>(11, 0.0);
  for (const auto &elem : *m_state.psl_per_barrier) {
    maxLength += elem.second;
    for (size_t nskip = 0; nskip < 11; nskip++) {
      weighted_avg_psl[nskip] += elem.first[nskip] * elem.second;
    }
  }

  if (maxLength != 0) {
    for (size_t nskip = 0; nskip < 11; nskip++) {
      weighted_avg_psl[nskip] = weighted_avg_psl[nskip] / static_cast<float>(maxLength + 1);
    }
  }
  m_psl_per_group.push_back(weighted_avg_psl);
}
