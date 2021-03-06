// Copyright (c) 2013 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef OPENMVG_ROBUST_ESTIMATION_GUIDED_MATCHING_H_
#define OPENMVG_ROBUST_ESTIMATION_GUIDED_MATCHING_H_

#include "openMVG/numeric/numeric.h"
#include "openMVG/matching/indMatch.hpp"
using namespace openMVG::matching;

#include "openMVG/features/regions.hpp"

#include <vector>

namespace openMVG{
namespace geometry_aware{

/// Guided Matching (features only):
///  Use a model to find valid correspondences:
///   Keep the best corresponding points for the given model under the
///   user specified distance.
template<
  typename ModelArg, // The used model type
  typename ErrorArg> // The metric to compute distance to the model
void GuidedMatching(
  const ModelArg & mod, // The model
  const Mat & xLeft,    // The left data points
  const Mat & xRight,   // The right data points
  double errorTh,       // Maximal authorized error threshold
  IndMatches & vec_corresponding_index) // Ouput corresponding index
{
  assert(xLeft.rows() == xRight.rows());

  // Looking for the corresponding points that have
  //  the smallest distance (smaller than the provided Threshold)

  for (size_t i = 0; i < xLeft.cols(); ++i) {

    double min = std::numeric_limits<double>::max();
    IndMatch match;
    for (size_t j = 0; j < xRight.cols(); ++j) {
      // Compute the geometric error: error to the model
      const double err = ErrorArg::Error(
        mod,  // The model
        xLeft.col(i), xRight.col(j)); // The corresponding points
      // if smaller error update corresponding index
      if (err < errorTh && err < min) {
        min = err;
        match = IndMatch(i,j);
      }
    }
    if (min < errorTh)  {
      // save the best corresponding index
      vec_corresponding_index.push_back(match);
    }
  }

  // Remove duplicates (when multiple points at same position exist)
  IndMatch::getDeduplicated(vec_corresponding_index);
}

// Struct to help filtering of correspondence according update of
//  two smallest distance.
// -> useful for descriptor distance ratio filtering
template <typename DistT>
struct distanceRatio
{
  DistT bd, sbd; // best and second best distance
  size_t idx; // best corresponding index

  distanceRatio():
    bd(std::numeric_limits<DistT>::max()),
    sbd(std::numeric_limits<DistT>::max()),
    idx(0)
  { }

  // Update match according the provided distance
  inline void update(size_t index, DistT dist)
  {
    if (dist < bd) // best than any previous
    {
      idx = index;
      // update and swap
      sbd = dist;
      std::swap(bd, sbd);
    }
    else if(dist < sbd)
    {
      sbd = dist;
    }
  }

  // Return if the ratio of distance is ok or not
  inline bool isValid(const double distRatio) const{
    // check:
    // - that two best distance have been found
    // - the distance ratio
    return
      (sbd != std::numeric_limits<DistT>::max()
      && bd < distRatio * sbd);
  }
};

/// Guided Matching (features + descriptors with distance ratio):
///  Use a model to find valid correspondences:
///   Keep the best corresponding points for the given model under the
///   user specified distance ratio.
template<
  typename ModelArg,    // The used model type
  typename ErrorArg,    // The metric to compute distance to the model
  typename DescriptorT, // The descriptor type
  typename MetricT >    // The metric to compare two descriptors
void GuidedMatching(
  const ModelArg & mod, // The model
  const Mat & xLeft,    // The left data points
  const std::vector<DescriptorT > & lDescriptors,
  const Mat & xRight,   // The right data points
  const std::vector<DescriptorT > & rDescriptors,
  double errorTh,       // Maximal authorized error threshold
  double distRatio,     // Maximal authorized distance ratio
  IndMatches & vec_corresponding_index) // Ouput corresponding index
{
  assert(xLeft.rows() == xRight.rows());
  assert(xLeft.cols() == lDescriptors.size());
  assert(xRight.cols() == rDescriptors.size());

  MetricT metric;

  // Looking for the corresponding points that have
  //  the satisfy:
  //   1. an geometric distance below the provided Threshold
  //   2. a distance ratio between descriptors of valid geometric correspondencess

  for (size_t i = 0; i < xLeft.cols(); ++i) {

    distanceRatio<typename MetricT::ResultType > dR;
    for (size_t j = 0; j < xRight.cols(); ++j) {
      // Compute the geometric error: error to the model
      const double geomErr = ErrorArg::Error(
        mod,  // The model
        xLeft.col(i), xRight.col(j)); // The corresponding points
      if (geomErr < errorTh) {
        const typename MetricT::ResultType descDist =
          metric( lDescriptors[i].getData(), rDescriptors[j].getData(), DescriptorT::static_size );
        // if smaller error update corresponding index
        dR.update(j, descDist);
      }
    }
    // Add correspondence only if if the distance ratio is valid
    if (dR.isValid(distRatio))  {
      // save the best corresponding index
      vec_corresponding_index.push_back(IndMatch(i,dR.idx));
    }
  }

  // Remove duplicates (when multiple points at same position exist)
  IndMatch::getDeduplicated(vec_corresponding_index);
}

/// Compute a bucket index from an epipolar point
///  (the one that is closer to image border intersection)
static unsigned int pix_to_bucket(const Vec2i &x, int W, int H)
{
	if (x(1) == 0) return x(0); // Top border
	if (x(0) == W-1) return W-1 + x(1); // Right border
	if (x(1) == H-1) return 2*W + H-3 - x(0); // Bottom border
	return 2*(W+H-2) - x(1); // Left border
}

/// Compute intersection of the epipolar line with the image border
static bool line_to_endPoints(const Vec3 & line, int W, int H, Vec2 & x0, Vec2 & x1)
{
  const double a = line(0), b = line(1), c = line(2);

  float r1, r2;
  // Intersection with Y axis (0 or W-1)
	if (b!=0)
	{
		double x = (b<0) ? 0 : W-1;
		double y = -(a*x+c)/b;
		if (y < 0) y = 0.;
		else if (y >= H) y = H-1;
		r1 = fabs(a*x + b*y + c);
		x0 << x,y;
	}
	else  {
		return false;
	}

	// Intersection with X axis (0 or H-1)
	if (a!=0)
	{
		double y = (a<0) ? H-1 : 0;
		double x = -(b*y+c)/a;
		if (x < 0) x = 0.;
		else if (x >= W) x = W-1;
		r2 = fabs(a*x + b*y + c);
		x1 << x,y;
	}
	else  {
    return false;
	}

	// Choose x0 to be as close as the intersection axis
	if (r1>r2)
    std::swap(x0,x1);

	return true;
}

/// Guided Matching (features + descriptors with distance ratio):
/// Cluster correspondences per epipolar line (faster than exhaustive search).
///   Keep the best corresponding points for the given model under the
///   user specified distance ratio.
/// Can be seen as a variant of geometry_aware method [1].
/// Note that implementation done here use a pixel grid limited to image border.
///
///  [1] Rajvi Shah, Vanshika Shrivastava, and P J Narayanan
///  Geometry-aware Feature Matching for Structure from Motion Applications.
///  WACV 2015.
template<
  typename ErrorArg,    // The metric to compute distance to the model
  typename MetricT >    // The metric to compare two descriptors
void GuidedMatching_Fundamental_Fast(
  const Mat3 & FMat,    // The fundamental matrix
  const Vec3 & epipole2,// Epipole2 (camera center1 in image plane2; must not be normalized)
  const features::Regions * lRegions,
  const int widthR, const int heightR,
  const features::Regions * rRegions,
  double errorTh,       // Maximal authorized error threshold (consider it's a square threshold)
  double distRatio,     // Maximal authorized distance ratio
  IndMatches & vec_corresponding_index) // Ouput corresponding index
{
  // Looking for the corresponding points that have
  //  the satisfy:
  //   1. an geometric distance below the provided Threshold
  //   2. a distance ratio between descriptors of valid geometric correspondencess
  //
  // - Cluster left point according their epipolar line border intersection.
  // - For each right point, compute threshold limited bandwidth and compare only
  //   points that belong to this range (limited buckets).

  const PointFeatures r_pt = rRegions->GetRegionsPositions();
  const PointFeatures l_pt = lRegions->GetRegionsPositions();

  MetricT metric;

  // Normalize F and epipole for (ep2->p2) line adequation
  Mat3 F = FMat;
  Vec3 ep2 = epipole2;
  if (ep2(2) > 0.0) {
    F = -F;
    ep2 = -ep2;
  }
  ep2 = ep2 / ep2(2);

  //--
  //-- Store point in the corresponding epipolar line bucket
  //--
  typedef std::vector<IndexT> Bucket_vec;
	typedef std::vector<Bucket_vec> Buckets_vec;
	const int nb_buckets = 2*(widthR + heightR-2);

  Buckets_vec buckets(nb_buckets);
  for (size_t i = 0; i < l_pt.size(); ++i) {

    // Compute epipolar line
    const Vec3 line = F * Vec3(l_pt[i].x(), l_pt[i].y(), 1.);
    // If epipolar line exist in Right image
    Vec2 x0, x1;
    if (line_to_endPoints(line, widthR, heightR, x0, x1))
    {
      // Find in which cluster the point belongs
      const int bucket = pix_to_bucket(x0.cast<int>(), widthR, heightR);
      buckets[bucket].push_back(i);
    }
  }

  // For each point in image right, find if there is good candidates.
  std::vector<distanceRatio<typename MetricT::ResultType > > dR(l_pt.size());
  for (size_t j = 0; j < r_pt.size(); ++j)
  {
    // According the point:
    // - Compute it's epipolar line from the epipole
    // - compute the range of possible bucket by computing
    //    the epipolar line gauge limitation introduced by the tolerated pixel error

    const Vec2 xR = r_pt[j].coords().cast<double>();
    const Vec3 l2 = ep2.cross(Vec3(xR(0), xR(1), 1.));
		const Vec2 n = l2.head<2>() * (sqrt(errorTh) / l2.head<2>().norm());

		const Vec3 l2min = ep2.cross(Vec3(xR(0) - n(0), xR(1) - n(1), 1.));
		const Vec3 l2max = ep2.cross(Vec3(xR(0) + n(0), xR(1) + n(1), 1.));

    // Compute corresponding buckets
		Vec2 x0, x1;
		if (!line_to_endPoints(l2min, widthR, heightR, x0, x1))
      continue;
    const int bucket_start = pix_to_bucket(x0.cast<int>(), widthR, heightR);

 		if (!line_to_endPoints(l2max, widthR, heightR, x0, x1))
      continue;
    const int bucket_stop = pix_to_bucket(x0.cast<int>(), widthR, heightR);

    if (bucket_stop - bucket_start > 0) // test candidate buckets
    for (Buckets_vec::const_iterator itBs = buckets.begin() + bucket_start;
      itBs != buckets.begin() + bucket_stop; ++itBs)
    {
      const Bucket_vec & bucket = *itBs;
      for(Bucket_vec::const_iterator itB = bucket.begin(); itB != bucket.end(); ++itB)
      {
        const IndexT i = *itB;
        // Compute descriptor distance
        const typename MetricT::ResultType descDist =
          metric(
            ((const typename MetricT::ElementType*) lRegions->DescriptorRawData()) + (i * lRegions->DescriptorLength()),
            ((const typename MetricT::ElementType*) rRegions->DescriptorRawData()) + (j * rRegions->DescriptorLength()),
            rRegions->DescriptorLength() );
        // Update the closest distance for later distance ratio computation
        dR[i].update(j, descDist);
      }
    }
  }
  // Check distance ratio validity
  for (size_t i=0; i < dR.size(); ++i)
  {
    if (dR[i].isValid(distRatio))
    {
      // save the best corresponding index
      vec_corresponding_index.push_back(IndMatch(i, dR[i].idx));
    }
  }
}

} // namespace geometry_aware
} // namespace openMVG
#endif // OPENMVG_ROBUST_ESTIMATION_GUIDED_MATCHING_H_
