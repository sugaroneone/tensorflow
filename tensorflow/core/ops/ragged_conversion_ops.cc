/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#include "tensorflow/core/framework/common_shape_fns.h"
#include "tensorflow/core/framework/op.h"
#include "tensorflow/core/framework/shape_inference.h"

namespace tensorflow {

using shape_inference::DimensionHandle;
using shape_inference::InferenceContext;
using shape_inference::ShapeHandle;

Status RaggedTensorToSparseShapeFn(InferenceContext* c);
Status RaggedTensorToVariantShapeFn(InferenceContext* c);
Status RaggedTensorFromVariantShapeFn(InferenceContext* c);

//==============================================================================
// Registered Ops
//==============================================================================

REGISTER_OP("RaggedTensorToSparse")
    .Input("rt_nested_splits: RAGGED_RANK * Tsplits")
    .Input("rt_dense_values: T")
    .Output("sparse_indices: int64")
    .Output("sparse_values: T")
    .Output("sparse_dense_shape: int64")
    .Attr("RAGGED_RANK: int >= 1")
    .Attr("T: type")
    .Attr("Tsplits: {int32, int64} = DT_INT64")
    .SetShapeFn(RaggedTensorToSparseShapeFn);

REGISTER_OP("RaggedTensorToVariant")
    .Input("rt_nested_splits: RAGGED_RANK * Tsplits")
    .Input("rt_dense_values: Tvalues")
    .Output("encoded_ragged: variant")
    .Attr("RAGGED_RANK: int >= 1")
    .Attr("Tvalues: type")
    .Attr("Tsplits: {int32, int64}")
    .Attr("batched_input: bool")
    .SetShapeFn(RaggedTensorToVariantShapeFn);

REGISTER_OP("RaggedTensorFromVariant")
    .Input("encoded_ragged: variant")
    .Output("output_nested_splits: output_ragged_rank * Tsplits")
    .Output("output_dense_values: Tvalues")
    .Attr("input_ragged_rank: int >= 0")
    .Attr("output_ragged_rank: int >= 1")
    .Attr("Tvalues: type")
    .Attr("Tsplits: {int32, int64}")
    .SetShapeFn(RaggedTensorFromVariantShapeFn);

//==============================================================================
// Shape Functions
//==============================================================================

Status RaggedTensorToSparseShapeFn(InferenceContext* c) {
  int64 num_splits;
  TF_RETURN_IF_ERROR(c->GetAttr<int64>("RAGGED_RANK", &num_splits));
  // TODO(b/112274756): Allow ragged_rank to be 0.
  if (num_splits < 1) {
    return errors::InvalidArgument("Requires RAGGED_RANK>0");
  }
  ShapeHandle rt_dense_values = c->input(num_splits);
  TF_RETURN_IF_ERROR(c->WithRankAtLeast(rt_dense_values, 1, &rt_dense_values));

  // Check that all rt_nested_splits have rank 1.
  for (int64 i = 0; i < num_splits; ++i) {
    ShapeHandle splits = c->input(i);
    TF_RETURN_IF_ERROR(c->WithRank(splits, 1, &splits));
  }

  DimensionHandle dense_dims =
      c->RankKnown(rt_dense_values)
          ? c->MakeDim(c->Rank(rt_dense_values) + num_splits)
          : c->UnknownDim();
  DimensionHandle num_values = c->NumElements(rt_dense_values);

  c->set_output(0, c->Matrix(num_values, dense_dims));  // indices
  c->set_output(1, c->Vector(num_values));              // values
  c->set_output(2, c->Vector(dense_dims));              // dense_shape

  return Status::OK();
}

Status RaggedTensorToVariantShapeFn(InferenceContext* c) {
  int64 num_splits;
  TF_RETURN_IF_ERROR(c->GetAttr<int64>("RAGGED_RANK", &num_splits));
  bool batched;
  TF_RETURN_IF_ERROR(c->GetAttr<bool>("batched_input", &batched));
  shape_inference::ShapeHandle rt_dense_values = c->input(num_splits);
  TF_RETURN_IF_ERROR(c->WithRankAtLeast(rt_dense_values, 1, &rt_dense_values));
  for (int64 i = 0; i < num_splits; ++i) {
    shape_inference::ShapeHandle splits = c->input(i);
    TF_RETURN_IF_ERROR(c->WithRank(splits, 1, &splits));
  }
  if (batched) {
    auto num_first_splits = c->Dim(c->input(0), 0);
    shape_inference::DimensionHandle num_rows;
    TF_RETURN_IF_ERROR(c->Subtract(num_first_splits, 1, &num_rows));
    c->set_output(0, c->Vector(num_rows));
  } else {
    c->set_output(0, c->Scalar());
  }
  return Status::OK();
}

Status RaggedTensorFromVariantShapeFn(InferenceContext* c) {
  int64 input_ragged_rank;
  TF_RETURN_IF_ERROR(
      c->GetAttr<int64>("input_ragged_rank", &input_ragged_rank));
  int64 output_ragged_rank;
  TF_RETURN_IF_ERROR(
      c->GetAttr<int64>("output_ragged_rank", &output_ragged_rank));
  shape_inference::ShapeHandle encoded_ragged = c->input(0);
  if (c->RankKnown(encoded_ragged)) {
    shape_inference::ShapeHandle unused;
    TF_RETURN_IF_ERROR(c->WithRank(
        encoded_ragged, output_ragged_rank - input_ragged_rank, &unused));
  }
  for (int64 i = 0; i < output_ragged_rank; i++) {
    c->set_output(i, c->UnknownShapeOfRank(1));
  }
  c->set_output(output_ragged_rank, c->UnknownShape());
  return Status::OK();
}

}  // namespace tensorflow
