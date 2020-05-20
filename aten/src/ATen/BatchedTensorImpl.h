#pragma once

#include <bitset>

#include <ATen/ArrayRef.h>
#include <ATen/SmallVector.h>
#include <ATen/Tensor.h>

namespace at {

// We assume this in a few other places in the codebase,
// but there isn't a centralized definition.
constexpr int64_t kVmapMaxTensorDims = 64;

// Store this number of elements of BatchDims on the stack. Most people will
// probably use <= 5 nested vmaps, but adjust this number as necessary.
constexpr int64_t kBatchDimsStackSize = 5;

// a BatchDim represents a "private" dimension on a Tensor created inside of
// vmap. It is a (level, dim) tuple, with the `dim` indicating which dimension
// is being vmap'ed over and the `level` being an identifier for which vmap
// said dimension was created inside.
struct BatchDim {
  BatchDim(int64_t level, int64_t dim) : dim_(dim), level_(level) {}
  int64_t dim() const {
    return dim_;
  }
  int64_t level() const {
    return level_;
  }
 private:
  int64_t dim_;
  int64_t level_;
};

using BatchDims = SmallVector<BatchDim, kBatchDimsStackSize>;
using BatchDimsRef = ArrayRef<BatchDim>;

// A BatchedTensorImpl holds an underlying Tensor and a list of BatchDim
//
// The batch dimensions are treated as being "private"; they are not user-visible.
// For example, in the following Tensor,
//    bt = BatchedTensorImpl(ones(2, 3, 5, 7), [(lvl=1, dim=0), (lvl=2, dim=1)])
// dimensions 0 and 1 are batch dimensions.
//
// bt.sizes() returns (5, 7); bt.sum(0) performs a reduction over the (public)
// dim 0, which is equivalent to dim 3 in the underlying ones(2, 3, 5, 7) tensor.
struct TORCH_API BatchedTensorImpl : public c10::TensorImpl {
  explicit BatchedTensorImpl(Tensor value, BatchDims bdims);

  // Returns a reference to BatchDims that represent which dimensions of this
  // tensor are private.
  BatchDimsRef bdims() const { return bdims_; }

  // BatchedTensorImpl wraps a Tensor
  const Tensor& value() const { return value_; };

  // Given a public dimension index, return the dimension index in the underlying
  // value() tensor.
  // For example, if we have
  //    bt = BatchedTensorImpl(ones(2, 3, 5, 7), [(lvl=1, dim=0), (lvl=2, dim=2)])
  // bt.actualDim(0) -> 1
  // bt.actualDim(1) -> 3
  // bt.actualDim(2) -> Error
  int64_t actualDim(int64_t dim, bool wrap_dim = true) const;

  // Override a bunch of methods inherited from TensorImpl to return error messages.
  bool is_contiguous(at::MemoryFormat memory_format=at::MemoryFormat::Contiguous) const override;
  IntArrayRef strides() const override;
  int64_t stride(int64_t d) const override;
  void set_size(int64_t dim, int64_t new_size) override;
  void set_stride(int64_t dim, int64_t new_stride) override;
  void set_storage_offset(int64_t storage_offset) override;
  bool has_storage() const override;
  const Storage& storage() const override;
  int64_t storage_offset() const override;

 private:
  Tensor value_;

  // NOTE: [BatchDims sorted by level invariant]
  // There is an invariant that the BatchDims must be stored in increasing `level`
  // order. That is, for i < j, bdims_[i].level must be less than bdims_[j].level.
  BatchDims bdims_;
};

inline bool isBatched(const Tensor& tensor) {
  return tensor.unsafeGetTensorImpl()->key_set().has(DispatchKey::Batched);
}

// It is unsafe to call this on a Tensor that is not backed by a
// BatchedTensorImpl. Please use `maybeGetBatched` whenever possible.
inline BatchedTensorImpl* unsafeGetBatched(Tensor tensor) {
  return static_cast<BatchedTensorImpl*>(tensor.unsafeGetTensorImpl());
}

inline BatchedTensorImpl* maybeGetBatched(Tensor tensor) {
  if (!isBatched(tensor)) {
    return nullptr;
  }
  return unsafeGetBatched(tensor);
}

// Returns a bitset. If bit i is set, then that means dim i is a batchdim.
inline std::bitset<kVmapMaxTensorDims> createBatchDimBitset(BatchDimsRef bdims) {
  std::bitset<kVmapMaxTensorDims> is_bdim;
  for (const auto& bdim : bdims) {
    is_bdim.set(bdim.dim());
  }
  return is_bdim;
}

inline std::ostream& operator<<(std::ostream& out, const BatchDim& bdim) {
  out << "(lvl=" << bdim.level() << ", dim=" << bdim.dim() << ")";
  return out;
}

inline Tensor makeBatched(const Tensor& tensor, BatchDims bdims) {
  TORCH_INTERNAL_ASSERT(!isBatched(tensor));
  auto tensor_dim = tensor.dim();
  TORCH_CHECK(
      tensor_dim <= kVmapMaxTensorDims,
      "vmap only supports tensors of dimensionality up to ", kVmapMaxTensorDims,
      "; got a tensor with dim ", tensor_dim);
  return at::detail::make_tensor<BatchedTensorImpl>(tensor, std::move(bdims));
}

// Adds a batch dim to `tensor`, returning a Tensor backed by a BatchedTensorImpl.
TORCH_API Tensor addBatchDim(const Tensor& tensor, int64_t level, int64_t dim);


}
