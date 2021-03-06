/*
 * reduce3.h
 *
 *  Created on: Dec 28, 2015
 *      Author: agibsonccc
 */

#ifndef REDUCE3_H_
#define REDUCE3_H_

#define EXTRA_PARAMS_LENGTH 10

#include <op.h>
#include <templatemath.h>
#include <helper_cuda.h>
#include <sharedmem.h>
#include <omp.h>
#include <pairwise_util.h>
#include <dll.h>
#include <shape.h>

#ifdef __JNI__
#include <jni.h>
#endif

#ifdef __CUDACC__
#include <cuda.h>
#include <cuda_runtime.h>
#endif

namespace functions {
	namespace reduce3 {

/**
 * Reduce involving
 * 2 arrays
 */
		template<typename T>
		class Reduce3: public virtual functions::ops::Op<T> {

		public:

			virtual
#ifdef __CUDACC__
			__host__  __device__

#endif
			inline T postProcess(T reduction, Nd4jIndex n,T **extraParamsRef) = 0;

			virtual
#ifdef __CUDACC__
			__inline__ __host__ __device__
#endif
			T startingValue(T *input) = 0;

			virtual
#ifdef __CUDACC__
			__inline__ __host__ __device__
#endif
			T * generateExtraParams() = 0;
			virtual
#ifdef __CUDACC__
			__inline__ __host__ __device__
#endif
			void finalizeExtraParams(T **extraParamsRef)  = 0;

			/**
             *
             * @param d1
             * @param d2
             * @param extraParams
             * @return
             */
			//an op for the kernel
			virtual
#ifdef __CUDACC__
			__host__  __device__

#endif
			inline T op(T d1, T d2, T **extraParamsRef) = 0;

			//calculate an update of the reduce operation
			/**
             *
             * @param old
             * @param opOutput
             * @param extraParams
             * @return
             */
			virtual
#ifdef __CUDACC__
			__host__  __device__

#endif
			inline T update(T old, T opOutput, T **extraParamsRef) = 0;

			/**
             *
             * @param old
             * @param opOutput
             * @param extraParams
             * @return
             */
			virtual
#ifdef __CUDACC__
			__host__  __device__

#endif
			inline T merge(T old, T opOutput, T **extraParamsRef) = 0;




			/**
             *
             * @param d1
             * @param d2
             * @param extraParams
             * @return
             */
			//an op for the kernel
#ifdef __CUDACC__
			virtual __device__

			inline T opAtomic(T d1, T d2, T **extraParamsRef) = 0;
#endif

#ifdef __CUDACC__
			/**
     * Aggregate shared memory
     * @param sPartialsRef
     * @param tid
     * @param extraParams
     */
			virtual __inline__ __device__ void aggregatePartials(T **sPartialsRef, int tid, T **extraParamsRef) {
				// start the shared memory loop on the next power of 2 less
				// than the block size.  If block size is not a power of 2,
				// accumulate the intermediate sums in the remainder range.
				T *sPartials = *sPartialsRef;
				int floorPow2 = blockDim.x;

				if (floorPow2 & (floorPow2 - 1)) {
					while (floorPow2 & (floorPow2 - 1)) {
						floorPow2 &= floorPow2 - 1;
					}
					if (tid >= floorPow2) {
						sPartials[tid - floorPow2] = update(sPartials[tid - floorPow2], sPartials[tid], extraParamsRef);
					}
					__syncthreads();
				}

				for (int activeThreads = floorPow2 >> 1; activeThreads; activeThreads >>= 1) {
					if (tid < activeThreads) {
						sPartials[tid] = update(sPartials[tid], sPartials[tid + activeThreads], extraParamsRef);
					}
					__syncthreads();
				}
			}
			/**

                     Perform a reduction
                     @param n the number of elements
                     @param xOffset the starting offset
                     @param dx the data to perform the reduction on
                     @param incx the increment on which to perform the reduction
                     @param extraParams extra parameters used for calculations
                     @param result where to store the result of the reduction
             */
			virtual __inline__ __device__ void transformNoElementWiseStride(
					T *dx,
					int *xShapeInfo,
					T *dy,
					int *yShapeInfo,
					T *extraParams,
					T *result,
					int *resultShapeInfo,
					int postProcessOrNot, int *allocationPointer, UnifiedSharedMemory<T> *manager) {
				Nd4jIndex n = shape::length(xShapeInfo);
				int rank = shape::rank(xShapeInfo);
				//shared memory space for storing intermediate results
				//SharedMemory <T> val;
				volatile T *sPartials = manager->getSharedReductionBuffer(); //val.getPointer();
				T startingVal = this->startingValue(dx);


				int numElements = gridDim.x;
				for (int i = threadIdx.x; i < numElements; i += blockDim.x)
					sPartials[i] = startingVal;
				__syncthreads();


#pragma unroll
				for(Nd4jIndex i = blockIdx.x * gridDim.x + threadIdx.x;i < n; i += gridDim.x * blockDim.x) {
					int *idx = shape::ind2sub(rank,shape::shapeOf(xShapeInfo),i);
					Nd4jIndex offset = shape::getOffset(0,shape::shapeOf(xShapeInfo),shape::stride(xShapeInfo),idx,rank);
					Nd4jIndex yOffset = shape::getOffset(0,shape::shapeOf(yShapeInfo),shape::stride(yShapeInfo),idx,rank);
					sPartials[threadIdx.x] = update(sPartials[threadIdx.x], this->opAtomic(dx[offset], dy[yOffset], &extraParams),&extraParams);
					delete[] idx;
				}

				T **sPartialsRef = (T **) &sPartials;
				aggregatePartials(sPartialsRef, threadIdx.x, &extraParams);
				/**
                 * Look at something that uses the extra params
                 * and aggregates the extra values propelry.
                 *This will be used in summary stats too.
                 */
				// write result for this block to global mem
				if (threadIdx.x == 0) {
					if (postProcessOrNot) {
						result[blockIdx.x] = postProcess(sPartials[0], n,&extraParams);
					}
					else {
						result[blockIdx.x] = sPartials[0];
					}


				}


				if(threadIdx.x == 0 && this->extraParamsLength() > 0)
					this->finalizeExtraParams(&extraParams);



			}


			/**
             *
             */
			virtual __inline__ __device__ void execScalarCuda(
					T *dx,
					int *xShapeInfo,
					T *dy,
					int *yShapeInfo,
					T *extraParams,
					T *result,
					int *resultShapeInfo, int *allocationBuffer, UnifiedSharedMemory<T> *manager) {

				if (threadIdx.x == 0)
					printf("Going for scalar reduce 3");

//		SharedMemory <T> val;
				volatile T *sPartials = manager->getSharedReductionBuffer(); // val.getPointer();

				T startingVal = this->startingValue(dx);
				Nd4jIndex length = shape::length(xShapeInfo);
				int xElementWiseStride = shape::elementWiseStride(xShapeInfo);
				int yElementWiseStride = shape::elementWiseStride(yShapeInfo);
				int tid = blockIdx.x * blockDim.x + threadIdx.x;
				char xOrder = shape::order(xShapeInfo);
				char yOrder = shape::order(yShapeInfo);
				if(xOrder == yOrder) {
					if (xElementWiseStride == 1 && yElementWiseStride == 1) {
						for(Nd4jIndex i = threadIdx.x; i < length; i+= gridDim.x * blockDim.x) {
							startingVal = update(startingVal, this->opAtomic(dx[i], dy[i], &extraParams), &extraParams);
						}
					}
					else {
						for(int i = threadIdx.x; i < length; i+= gridDim.x * blockDim.x) {
							startingVal = update(startingVal, this->opAtomic(dx[i * xElementWiseStride], dy[i * yElementWiseStride], &extraParams), &extraParams);
						}
					}

					sPartials[tid] = startingVal;
					__syncthreads();


					T **sPartialsRef = (T **) &sPartials;
					aggregatePartials(sPartialsRef, tid, &extraParams);

					/**
                     * Look at something that uses the extra params
                     * and aggregates the extra values properly.
                     *This will be used in summary stats too.
                     */
					// write result for this block to global mem
					__syncthreads();
					if (tid == 0) {
						result[0] = postProcess(sPartials[0], length,&extraParams);
					}
				}

				else {
					int *xShape = shape::shapeOf(xShapeInfo);
					int *xStride = shape::stride(xShapeInfo);
					int *yStride = shape::stride(yShapeInfo);
					T startingVal = this->startingValue(dx);
					int n = shape::length(xShapeInfo);

					//SharedMemory <T> val;
					volatile T *sPartials = manager->getSharedReductionBuffer(); //val.getPointer();


					Nd4jIndex length = shape::length(xShapeInfo);
					int xElementWiseStride = shape::elementWiseStride(xShapeInfo);
					int yElementWiseStride = shape::elementWiseStride(yShapeInfo);
					char xOrder = shape::order(xShapeInfo);
					char yOrder = shape::order(yShapeInfo);


					//int *idx = (int *) malloc(sizeof(int) * shape::rank(xShapeInfo));
					int rank = shape::rank(xShapeInfo);
					/*
					long allocSize = sizeof(int) * rank;
					int *idx = shape::cuMalloc(allocationBuffer, allocSize, manager);
					*/
					int idx[MAX_RANK];

					//shared memory space for storing intermediate results
					sPartials[threadIdx.x] = startingVal;


#pragma unroll
					for(unsigned int i = tid ;i < n; i += gridDim.x * blockDim.x) {
						shape::ind2sub(rank,shape::shapeOf(xShapeInfo),i,idx);
						Nd4jIndex offset = shape::getOffset(0,shape::shapeOf(xShapeInfo),shape::stride(xShapeInfo),idx,rank);
						Nd4jIndex yOffset = shape::getOffset(0,shape::shapeOf(yShapeInfo),shape::stride(yShapeInfo),idx,rank);
						sPartials[threadIdx.x] = update(sPartials[threadIdx.x], this->opAtomic(dx[offset], dy[yOffset], &extraParams),&extraParams);
					}

/*
					if (rank > MAX_COORD && tid * allocSize > PREALLOC_SIZE - allocSize) {
						free(idx);
					}
*/

					T **sPartialsRef = (T **) &sPartials;
					aggregatePartials(sPartialsRef, threadIdx.x, &extraParams);
					/**
                     * Look at something that uses the extra params
                     * and aggregates the extra values propelry.
                     *This will be used in summary stats too.
                     */
					// write result for this block to global mem
					__syncthreads();
					if (tid == 0) {
						result[tid] = postProcess(sPartials[0], n,&extraParams);
					}
				}


			}
			/**
                     Perform a reduction
                     @param n the number of elements
                     @param xOffset the starting offset
                     @param dx the data to perform the reduction on
                     @param incx the increment on which to perform the reduction
                     @param extraParams extra parameters used for calculations
                     @param result where to store the result of the reduction
             */
			virtual __inline__ __device__ void transform(
					T *dx,
					int *xShapeInfo,
					T *dy,
					int *yShapeInfo,
					T *extraParams,
					T *result,
					int *resultShapeInfo,
					int *dimension,
					int dimensionLength,
					int postProcessOrNot,
					int *allocationPointer, UnifiedSharedMemory<T> *manager) {
				/**
                 * Gpu information for the problem
                 */
				int tid = threadIdx.x + blockIdx.x * blockDim.x;

				__shared__ volatile int resultScalar;

				__shared__ int xElementWiseStride;
				__shared__ int yElementWiseStride;
				//shared memory space for storing intermediate results
				//SharedMemory <T> val;
				volatile T *sPartials = manager->getSharedReductionBuffer(); //val.getPointer();
				T init = this->startingValue(dx);
				sPartials[threadIdx.x] = init;


				//length for the tad

				__shared__ Nd4jIndex resultLength;


				T reduction = this->startingValue(dx);
				if (threadIdx.x == 0) {
					if (resultShapeInfo != nullptr)
						resultLength = shape::length(resultShapeInfo);
					else resultLength = 1;

					if (dimensionLength == 1) {
						if (dimension == nullptr || dimension[0] == MAX_DIMENSION)
							resultScalar = 1;
						else
							resultScalar = 0;
					}
					else
						resultScalar = 0;

					if (resultLength == 1)
						resultScalar = 1;
					/**
                     * The element wise stride belong longs to a reduction index.
                     * When used out of order, we can get rid of the data
                     * dependencies and rely on using the max dimension
                     * specified for stride instead.
                     * Say we take the sum(0,1) along long arr
                     * we can use arr.stride(1) as a representation
                     * along long which to iterate.
                     */


					int *xStride = shape::stride(xShapeInfo);
					char xOrder = shape::order(xShapeInfo);



					xElementWiseStride = shape::elementWiseStride(xShapeInfo);
					yElementWiseStride = shape::elementWiseStride(yShapeInfo);


					//printf("Order is: [%c], stride is: xElementStride: [%i], passed strides are: [%i], dimension: [%i], dimensionLength: [%i]\n", xOrder, xElementWiseStride, xStride[0], dimension[0], dimensionLength);
				}
				__syncthreads();


				if (!resultScalar) {
					__shared__ shape::TAD *tad;
                    if (threadIdx.x == 0) {
                        tad = new(manager->getTADSpace()) shape::TAD(); //(xShapeInfo,dimension,dimensionLength)
                        tad->setExternalBuffers((void *) manager);
                        tad->init(xShapeInfo,dimension,dimensionLength);
                        tad->createTadOnlyShapeInfo();
                    }
                    __syncthreads();


					if(dimensionLength > 1) {
						//decompose in to several sub tads after
						//moving all dimensions (in sorted order)
						//to the back.
						//permuted version of the x shape info for setting up the tad problem

						__shared__ int *tadShapeShapeInfo;
						__shared__ int *inputShapeInfo;

						if(tid == 0) {
							inputShapeInfo = xShapeInfo;
						}
						__syncthreads();


						//decompose in to several sub tads after
						//moving all dimensions (in sorted order)
						//to the back.
						//permuted version of the x shape info for setting up the tad problem
						if(tid == 0)
							tadShapeShapeInfo = tad->tadOnlyShapeInfo;//shape::shapeInfoOnlyShapeAndStride(xShapeInfo,dimension,dimensionLength,false);
						__syncthreads();

						int *xShape = shape::shapeOf(tadShapeShapeInfo);
						int *xStride = shape::stride(tadShapeShapeInfo);
						Nd4jIndex tadLength = shape::length(tadShapeShapeInfo);
						int rank = shape::rank(tadShapeShapeInfo);
#pragma unroll
						for(Nd4jIndex i = tid; i < resultLength; i+= gridDim.x * blockDim.x) {
							int offset = tad->tadOffset(i);
							int shapeIter[MAX_RANK];
							int coord[MAX_RANK];
							int dim;
							int xStridesIter[MAX_RANK];
							int yStridesIter[MAX_RANK];
							T *xPointer = dx + offset;
							T start = this->startingValue(xPointer);
							int *xShape = shape::shapeOf(xShapeInfo);
							int *xStride = shape::stride(xShapeInfo);
							int *yStride = shape::stride(yShapeInfo);
							T startingVal = this->startingValue(dx);
							Nd4jIndex n = shape::length(xShapeInfo);
							int rank = shape::rank(xShapeInfo);
							if(PrepareTwoRawArrayIter<T>(rank,
														 xShape,
														 dx,
														 xStride,
														 dy,
														 yStride,
														 &rank,
														 shapeIter,
														 &dx,
														 xStridesIter,
														 &dy,
														 yStridesIter) >= 0) {
								ND4J_RAW_ITER_START(dim, rank, coord, shapeIter); {
										/* Process the innermost dimension */
										T *xIter = dx;
										T *yIter = dy;
										startingVal = update(startingVal, op(xIter[0],yIter[0],&extraParams),&extraParams);
									} ND4J_RAW_ITER_TWO_NEXT(dim,
															 rank,
															 coord,
															 shapeIter,
															 dx,
															 xStridesIter,
															 dy,
															 yStridesIter);

								result[i] = postProcess(startingVal,n,&extraParams);
							}
							else {
								printf("Unable to prepare array\n");
							}

						}

						__syncthreads();

						if (tid == 0) {
						//	delete[] tadShapeShapeInfo;
						}


					}
					else {

						/**
                         * The element wise stride belong longs to a reduction index.
                         * When used out of order, we can get rid of the data
                         * dependencies and rely on using the max dimension
                         * specified for stride instead.
                         * Say we take the sum(0,1) along long arr
                         * we can use arr.stride(1) as a representation
                         * along long which to iterate.
                         */
						Nd4jIndex xLength = shape::length(xShapeInfo);
						Nd4jIndex tadLength = xLength / resultLength;

						Nd4jIndex i = 0,j = 0;
/*
						for (int r = blockIdx.x; r < tad->numTads; r += gridDim.x) {
                            if (threadIdx.x == 0)
                                tad->createOffsetForBlock(r);
                            __syncthreads();

                            int tadOffsetForBlock = tad->tadOffsetForBlock;
                            T *xVal = dx + tadOffsetForBlock;


                            sPartials[threadIdx.x] = this->startingValue(xVal);
                            for(int i = threadIdx.x; i < tad->tadLength; i+= blockDim.x) {
                    			int xOffsetForTad = shape::tadOffset(i, xShapeInfo, dimension, dimensionLength, nullptr);
								int yOffsetForTad = shape::tadOffset(i, yShapeInfo, dimension, dimensionLength, nullptr);

                                sPartials[threadIdx.x] = this->update(sPartials[threadIdx.x],dx[tadOffsetForBlock + i *  tad->tadElementWiseStride], extraParams);
                            }
                            __syncthreads();

                            // aggregate. do NOT reduce for elements > tadLength
                            T **sPartialsRef = (T **) &sPartials;
                            aggregatePartials(sPartialsRef, threadIdx.x, nd4j::math::nd4j_min<int>(blockDim.x, tad->tadLength), extraParams);


                            __syncthreads();
                            if (threadIdx.x == 0)
                                result[r] = this->postProcess(sPartials[threadIdx.x], tad->tadLength, extraParams);
                        }

*/

						for(i = tid; i < resultLength; i+= blockDim.x * gridDim.x) {
							int xOffsetForTad = tad->tadOffset(i);
							int yOffsetForTad = yOffsetForTad;//tad->tadOffset(i);
							//int xOffsetForTad = shape::tadOffset(i, xShapeInfo, dimension, dimensionLength, nullptr);
							//int yOffsetForTad = shape::tadOffset(i, yShapeInfo, dimension, dimensionLength, nullptr);

							sPartials[tid] = op(dx[xOffsetForTad],dy[yOffsetForTad], &extraParams);
							for(j = 1; j < tadLength; j++) {
								sPartials[i] =  update(sPartials[i],op(dx[xOffsetForTad + xElementWiseStride * j],dy[yOffsetForTad + yElementWiseStride * j], &extraParams), &extraParams);
							}

							printf("Updating result: [%i] -> [%f]\n", i, sPartials[i]);
							result[i] = postProcess(sPartials[i],tadLength,&extraParams);
						}

					}
				}
				else {
					this->execScalarCuda(
							dx,
							xShapeInfo,
							dy,
							yShapeInfo,
							extraParams,
							result,
							resultShapeInfo, allocationPointer, manager);
				}

			}





#endif
			/**
             *
             * @param x
             * @param xShapeInfo
             * @param extraParamsVals
             * @param y
             * @param yShapeInfo
             * @param result
             * @param resultShapeInfo
             */
#ifdef __CUDACC__
			__host__
#endif
			T execScalar(
					T *x,
					int *xShapeInfo,
					T *extraParamsVals,
					T *y,
					int *yShapeInfo) {
				T startingVal = this->startingValue(x);
				Nd4jIndex length = shape::length(xShapeInfo);
				int xElementWiseStride = shape::elementWiseStride(xShapeInfo);
				int yElementWiseStride = shape::elementWiseStride(yShapeInfo);
#pragma omp parallel for
				for(int i = 0; i < this->extraParamsLength();i++) {
					extraParamsVals[i] = startingVal;
				}

				char xOrder = shape::order(xShapeInfo);
				char yOrder = shape::order(yShapeInfo);
				if(xOrder == yOrder) {
					if (xElementWiseStride == 1 && yElementWiseStride == 1) {
						if(length < 8000) {
#pragma omp simd
							for(int i = 0; i < length; i++) {
								startingVal = update(startingVal,op(x[i],y[i],&extraParamsVals),&extraParamsVals);
							}

						}
						else {
#pragma omp parallel for shared(extraParamsVals)
							for(Nd4jIndex i = 0; i < length; i++) {
#pragma omp critical
								{
									startingVal = update(startingVal,op(x[i],y[i],&extraParamsVals),&extraParamsVals);

								}
							}

						}


						return postProcess(startingVal, length,&(extraParamsVals));

					}

					else {
						if(length < 8000) {
#pragma omp simd
							for(int i = 0; i < length; i++) {
								startingVal = update(startingVal,op(x[i * xElementWiseStride],y[i * yElementWiseStride],&extraParamsVals),&extraParamsVals);


							}

						}
						else {
#pragma omp parallel for shared(extraParamsVals)
							for(Nd4jIndex i = 0; i < length; i++) {
#pragma omp critical
								{
									startingVal = update(startingVal,op(x[i * xElementWiseStride],y[i * yElementWiseStride],&extraParamsVals),&extraParamsVals);

								}
							}
						}

						return  postProcess(startingVal, length,&(extraParamsVals));
					}

				}


				else {
					int *xShape = shape::shapeOf(xShapeInfo);
					int *xStride = shape::stride(xShapeInfo);
					int *yStride = shape::stride(yShapeInfo);
					T startingVal = this->startingValue(x);
					Nd4jIndex n = shape::length(xShapeInfo);
					int shapeIter[MAX_RANK];
					int coord[MAX_RANK];
					int dim;
					int xStridesIter[MAX_RANK];
					int yStridesIter[MAX_RANK];
					int rank = shape::rank(xShapeInfo);
					if(PrepareTwoRawArrayIter<T>(rank,
												 xShape,
												 x,
												 xStride,
												 y,
												 yStride,
												 &rank,
												 shapeIter,
												 &x,
												 xStridesIter,
												 &y,
												 yStridesIter) >= 0) {
						ND4J_RAW_ITER_START(dim, rank, coord, shapeIter); {
								/* Process the innermost dimension */
								T *xIter = x;
								T *yIter = y;
								startingVal = update(startingVal, op(xIter[0],yIter[0],&extraParamsVals),&extraParamsVals);
							} ND4J_RAW_ITER_TWO_NEXT(dim,
													 rank,
													 coord,
													 shapeIter,
													 x,
													 xStridesIter,
													 y,
													 yStridesIter);

						return postProcess(startingVal,n,&extraParamsVals);
					}
					else {
						printf("Unable to prepare array\n");
					}

				}

				return startingVal;


			}


			/**
             *
             * @param x
             * @param xShapeInfo
             * @param extraParamsVals
             * @param y
             * @param yShapeInfo
             * @param result
             * @param resultShapeInfo
             */
#ifdef __CUDACC__
			__host__
#endif
			void execScalar(
					T *x,
					int *xShapeInfo,
					T *extraParamsVals,
					T *y,
					int *yShapeInfo,
					T *result,
					int *resultShapeIfo) {
				result[0] = execScalar(x,xShapeInfo,extraParamsVals,y,yShapeInfo);
			}
			/**
             *
             * @param x
             * @param xShapeInfo
             * @param extraParamsVals
             * @param y
             * @param yShapeInfo
             * @param result
             * @param resultShapeInfo
             */
#ifdef __CUDACC__
			__host__
#endif
			void exec(
					T *x,
					int *xShapeInfo,
					T *extraParamsVals,
					T *y, int *yShapeInfo,
					T *result, int *resultShapeInfo) {

				execScalar(
						x,
						xShapeInfo,
						extraParamsVals,
						y,
						yShapeInfo,
						result,
						resultShapeInfo);
			}

			/**
             *
             * @param x
             * @param xShapeInfo
             * @param extraParamsVals
             * @param y
             * @param yShapeInfo
             * @param result
             * @param resultShapeInfoBuffer
             * @param dimension
             * @param dimensionLength
             */
			void exec(T *x, int *xShapeInfo,
					  T *extraParamsVals,
					  T *y,
					  int *yShapeInfo,
					  T *result,
					  int *resultShapeInfoBuffer,
					  int *dimension,
					  int dimensionLength) {
				if(shape::isScalar(resultShapeInfoBuffer)) {
					execScalar(
							x,
							xShapeInfo,
							extraParamsVals,
							y,
							yShapeInfo,
							result,
							resultShapeInfoBuffer);
					return;
				}



				char xOrder = shape::order(xShapeInfo);
				char yOrder = shape::order(yShapeInfo);
				if(xOrder != yOrder) {
					int shapeIter[MAX_RANK];
					int coord[MAX_RANK];
					int dim;
					int xStridesIter[MAX_RANK];
					int yStridesIter[MAX_RANK];

					int *xShape = shape::shapeOf(xShapeInfo);

					int *xStride = shape::stride(xShapeInfo);
					int *yStride = shape::stride(yShapeInfo);

					int rank = shape::rank(xShapeInfo);
					if(PrepareTwoRawArrayIter<T>(rank,
												 xShape,
												 x,
												 xStride,
												 y,
												 yStride,
												 &rank,
												 shapeIter,
												 &x,
												 xStridesIter,
												 &y,
												 yStridesIter) >= 0) {

						Nd4jIndex resultLength = shape::length(resultShapeInfoBuffer);
						Nd4jIndex tadLength = shape::tadLength(xShapeInfo,dimension,dimensionLength);
						ND4J_RAW_ITER_START(dim, rank, coord, shapeIter); {
								/* Process the innermost dimension */
								T *xIter = x;
								T *yIter = y;
								Nd4jIndex xOffset = shape::getOffset(0,xShape,xStride,coord,rank);
								int reductionIndex = xOffset / resultLength;
								result[reductionIndex] = update(result[reductionIndex],op(xIter[0],yIter[0],&extraParamsVals),&extraParamsVals);
							} ND4J_RAW_ITER_TWO_NEXT(dim,
													 rank,
													 coord,
													 shapeIter,
													 x,
													 xStridesIter,
													 y,
													 yStridesIter);


#pragma  omp parallel for
						for(Nd4jIndex i = 0; i < resultLength ;i++) {
							result[i] = postProcess(result[i],tadLength,&extraParamsVals);
						}
					}

					else {
						printf("Unable to prepare array\n");
					}
				}
				else {
					T startingVal = this->startingValue(x);

					Nd4jIndex resultLength = shape::length(resultShapeInfoBuffer);
					shape::TAD xTad(yShapeInfo,dimension,dimensionLength);
					xTad.createTadOnlyShapeInfo();
					xTad.createOffsets();

					/**
                     * The element wise stride belong longs to a reduction index.
                     * When used out of order, we can get rid of the data
                     * dependencies and rely on using the max dimension
                     * specified for stride instead.
                     * Say we take the sum(0,1) along long arr
                     * we can use arr.stride(1) as a representation
                     * along long which to iterate.
                     */
					int tadElementWiseStride = shape::elementWiseStride(xTad.tadOnlyShapeInfo);
					int tadLength = shape::length(xTad.tadOnlyShapeInfo);
#pragma omp parallel for
					for(Nd4jIndex i = 0; i < resultLength; i++) {
						T *localExtraParams = nullptr;
						if(this->extraParamsLength() > 0)
							localExtraParams = new T[this->extraParamsLength()];
						for(int extraParamsIdx = 0; extraParamsIdx < this->extraParamsLength(); extraParamsIdx++) {
							localExtraParams[extraParamsIdx] = startingVal;
						}

						Nd4jIndex offset = xTad.tadOffsets[i];
						result[i] = op(x[offset], y[offset],&localExtraParams);
						for(int j = 1; j < tadLength; j++) {
							result[i] =  update(result[i],op(x[offset + tadElementWiseStride * j],y[offset + tadElementWiseStride * j], &localExtraParams), &localExtraParams);
						}

						result[i] = postProcess(result[i],tadLength,&localExtraParams);

						if(localExtraParams != nullptr)
							delete[] localExtraParams;
					}

				}


			}



#ifdef __CUDACC__
			__host__ __device__
#endif
			virtual ~Reduce3() {
			}

#ifdef __CUDACC__
			__host__ __device__
#endif
			Reduce3() {
			}

		};

		namespace ops {
/**
 * Cosine similarity between 2
 * arrays
 */
			template<typename T>
			class CosineSimilarity: public virtual Reduce3<T> {
			public:

				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T * generateExtraParams() {
					T *extraParams = new T[2];
					return extraParams;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				void finalizeExtraParams(T **extraParams)  {
					delete[] *extraParams;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T startingValue(T *input) {
					return 0.0;
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				inline T postProcess(T reduction, Nd4jIndex n,T **extraParamsRef) {
					T *extraParams = *extraParamsRef;
					return reduction / (nd4j::math::nd4j_sqrt<T>(extraParams[0]) * nd4j::math::nd4j_sqrt<T>(extraParams[1]));
				}
				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T op(T d1, T d2, T **extraParamsRef) {
					T *extraParams = *extraParamsRef;
					extraParams[0] += d1 * d1;
					extraParams[1] += d2 * d2;
					return (d1 * d2);
				}


#ifdef __CUDACC__
				__host__ __device__
#endif
				void aggregateExtraParams(T **extraParamsTotal,T **extraParamsLocal) {
					T *extraParamsTotalRef = *extraParamsTotal;
					T *extraParamsLocalRef = *extraParamsLocal;
					extraParamsTotalRef[0] += extraParamsLocalRef[0];
					extraParamsTotalRef[1] += extraParamsLocalRef[1];

				}


				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel
#ifdef __CUDACC__
				virtual __device__
				inline T opAtomic(T d1, T d2, T **extraParamsRef) {
					T *extraParams = *extraParamsRef;

					nd4j::math::atomics::nd4j_atomicAdd(&extraParams[0],d1 * d1);
					nd4j::math::atomics::nd4j_atomicAdd(&extraParams[1],d2 * d2);

					return (d1 * d2);
				}
#endif
				//calculate an update of the reduce operation
				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T update(T old, T opOutput, T **extraParamsRef) {
					return old + opOutput;
				}

				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T merge(T old, T opOutput, T **extraParamsRef) {
					return update(old, opOutput, extraParamsRef);
				}





#ifdef __CUDACC__
				__host__ __device__
#endif
				virtual ~CosineSimilarity() {
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				CosineSimilarity() {
					this->extraParamsLen = 2;
				}
			};


/**
 * Dot product between 2 arrays
 */
			template<typename T>
			class Dot: public virtual Reduce3<T> {
			public:
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T * generateExtraParams() {
					return nullptr;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				void finalizeExtraParams(T **extraParamsRef)  {
					//no-op
					delete[] *extraParamsRef;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T startingValue(T *input) {
					return 0.0;
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				inline T postProcess(T reduction, Nd4jIndex n,T **extraParamsRef) {
					return reduction;
				}
				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T op(T d1, T d2, T **extraParamsRef) {
					return d1 * d2;
				}

				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel

#ifdef __CUDACC__
				virtual
				__device__


				inline T opAtomic(T d1, T d2, T **extraParamsRef) {
					return op(d1,d2,extraParamsRef);
				}
#endif

				//calculate an update of the reduce operation
				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T update(T old, T opOutput, T **extraParamsRef) {
					return opOutput + old;
				}

				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T merge(T old, T opOutput, T **extraParamsRef) {
					return update(old, opOutput, extraParamsRef);
				}

#ifdef __CUDACC__
				__host__ __device__
#endif
				void aggregateExtraParams(T **extraParamsTotal,T **extraParamsLocal) {
					//no extra params aggregation needs to happen
				}


#ifdef __CUDACC__
				__host__ __device__
#endif
				virtual ~Dot() {
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				Dot() {
				}
			};



/**
 * Euclidean distance between 2 arrays
 */
			template<typename T>
			class EuclideanDistance: public virtual Reduce3<T> {
			public:
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T * generateExtraParams() {
					return nullptr;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				void finalizeExtraParams(T **extraParamsRef)  {
					//no-op
					delete[] *extraParamsRef;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T startingValue(T *input) {
					return 0.0;
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				inline T postProcess(T reduction, Nd4jIndex n,T **extraParamsRef) {
					return nd4j::math::nd4j_sqrt<T>(reduction);
				}
				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T op(T d1, T d2, T **extraParamsRef) {
					T ret = d1 - d2;
					return ret * ret;
				}

				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel

#ifdef __CUDACC__
				virtual
				__device__


				inline T opAtomic(T d1, T d2, T **extraParamsRef) {
					return op(d1,d2,extraParamsRef);
				}
#endif

				//calculate an update of the reduce operation
				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T update(T old, T opOutput, T **extraParamsRef) {
					return opOutput + old;
				}

				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T merge(T old, T opOutput, T **extraParamsRef) {
					return update(old, opOutput, extraParamsRef);
				}

#ifdef __CUDACC__
				__host__ __device__
#endif
				void aggregateExtraParams(T **extraParamsTotal,T **extraParamsLocal) {
					//no extra params aggregation needs to happen
				}


#ifdef __CUDACC__
				__host__ __device__
#endif
				virtual ~EuclideanDistance() {
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				EuclideanDistance() {
				}
			};


/**
 * Manhattan distance between 2 arrays
 */
			template<typename T>
			class ManhattanDistance: public virtual Reduce3<T> {
			public:
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T * generateExtraParams() {
					return nullptr;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				void finalizeExtraParams(T **extraParamsRef)  {
					//no op
					delete[] *extraParamsRef;
				}
				virtual
#ifdef __CUDACC__
				__inline__ __host__ __device__
#endif
				T startingValue(T *input) {
					return 0.0;
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				inline T postProcess(T reduction, Nd4jIndex n,T **extraParamsRef) {
					return reduction;
				}
				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T op(T d1, T d2, T **extraParamsRef) {
					return nd4j::math::nd4j_abs<T>(d1 - d2);
				}

				//calculate an update of the reduce operation
				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T update(T old, T opOutput, T **extraParamsRef) {
					return old + opOutput;
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				void aggregateExtraParams(T **extraParamsTotal,T **extraParamsLocal) {
					//no extra params aggregation needs to happen
				}
				/**
                 *
                 * @param d1
                 * @param d2
                 * @param extraParams
                 * @return
                 */
				//an op for the kernel

#ifdef __CUDACC__
				virtual	__device__


				inline T opAtomic(T d1, T d2, T **extraParamsRef) {
					return op(d1,d2,extraParamsRef);
				}
#endif

				/**
                 *
                 * @param old
                 * @param opOutput
                 * @param extraParams
                 * @return
                 */
				virtual
#ifdef __CUDACC__
				__host__  __device__

#endif
				inline T merge(T old, T opOutput, T **extraParamsRef) {
					return update(old, opOutput, extraParamsRef);
				}

#ifdef __CUDACC__
				__host__ __device__
#endif
				virtual ~ManhattanDistance() {
				}
#ifdef __CUDACC__
				__host__ __device__
#endif
				ManhattanDistance() {
				}
			};

		}

		template<typename T>
		class Reduce3OpFactory {
		public:

#ifdef __CUDACC__
			__host__ __device__
#endif
			Reduce3OpFactory() {
			}


			/**
             * Create an op given an op number
             * @param op the op number
             * 0: manhattan distance
             * 1: euclidean distance
             * 2: cosine similarity
             * @return
             */
#ifdef __CUDACC__
			__inline__ __device__
			Reduce3<T> * getOp(int op, unsigned char *buffer) {
#else
				Reduce3<T> * getOp(int op) {
#endif
				if (op == 0)
#ifdef __CUDACC__
					return new(buffer) functions::reduce3::ops::ManhattanDistance<T>();
#else
					return new functions::reduce3::ops::ManhattanDistance<T>();
#endif
				else if (op == 1)
#ifdef __CUDACC__
					return new(buffer) functions::reduce3::ops::EuclideanDistance<T>();
#else
					return new functions::reduce3::ops::EuclideanDistance<T>();
#endif
				else if (op == 2)
#ifdef __CUDACC__
					return new(buffer) functions::reduce3::ops::CosineSimilarity<T>();
#else
					return new functions::reduce3::ops::CosineSimilarity<T>();
#endif
				else if (op == 3)
#ifdef __CUDACC__
					return new(buffer) functions::reduce3::ops::Dot<T>();
#else
				return new functions::reduce3::ops::Dot<T>();
#endif
				return nullptr;
			}
		};

	}
}

#ifdef __CUDACC__
template <typename T>
__inline__ __device__ void reduce3NoElementWiseStrideGeneric(
		int opNum,
		T *dx,
		int *xShapeInfo,
		T *dy,
		int *yShapeInfo,
		T *extraParams,
		T *result,
		int *resultShapeInfo,
		int postProcessOrNot, int *allocationPointer) {

	__shared__ functions::reduce3::Reduce3<T> * op;
	__shared__ functions::reduce3::Reduce3OpFactory<T> *reduce3OpFactory;

	__shared__ UnifiedSharedMemory<T> *manager;

	if (threadIdx.x == 0) {
		extern __shared__ unsigned char shmem[];
		manager = new(shmem) UnifiedSharedMemory<T>();
		manager->init(sizeof(UnifiedSharedMemory<T>), sizeof(functions::reduce3::Reduce3OpFactory<T>), sizeof(functions::reduce3::Reduce3<T>), sizeof(shape::TAD));
	}
	__syncthreads();

	__shared__ int *ptrSharedXShapeInfo;
	__shared__ int *ptrSharedYShapeInfo;
	__shared__ int *ptrSharedZShapeInfo;

	if (xShapeInfo != nullptr) {
		shape::sweepShapeInfoBuffer(xShapeInfo, manager->getXShapeBuffer());
		if (threadIdx.x == 0) ptrSharedXShapeInfo = manager->getXShapeBuffer();
	} else if (threadIdx.x == 0) ptrSharedXShapeInfo = nullptr;

	if (yShapeInfo != nullptr) {
		shape::sweepShapeInfoBuffer(yShapeInfo, manager->getYShapeBuffer());
		if (threadIdx.x == 0) ptrSharedYShapeInfo = manager->getYShapeBuffer();
	} else if (threadIdx.x == 0) ptrSharedYShapeInfo = nullptr;

	if (resultShapeInfo != nullptr) {
		shape::sweepShapeInfoBuffer(resultShapeInfo, manager->getZShapeBuffer());
		if (threadIdx.x == 0) ptrSharedZShapeInfo = manager->getZShapeBuffer();
	} else if (threadIdx.x == 0) ptrSharedZShapeInfo = nullptr;

	if(threadIdx.x == 0) {
		reduce3OpFactory = new(manager->getFactorySpace()) functions::reduce3::Reduce3OpFactory<T>();
		op = reduce3OpFactory->getOp(opNum, manager->getFunctionSpace());
	}
	__syncthreads();

	op->transformNoElementWiseStride(dx,ptrSharedXShapeInfo,dy,ptrSharedYShapeInfo,extraParams,result,ptrSharedZShapeInfo,postProcessOrNot, allocationPointer, manager);
}


__global__ void reduce3NoElementWiseStrideDouble(
		int opNum,
		double *dx,
		int *xShapeInfo,
		double *dy,
		int *yShapeInfo,
		double *extraParams,
		double *result,
		int *resultShapeInfo,
		int postProcessOrNot, int *allocationPointer) {
	reduce3NoElementWiseStrideGeneric<double>(
			opNum,
			dx,
			xShapeInfo,
			dy,
			yShapeInfo,
			extraParams,
			result,
			resultShapeInfo,
			postProcessOrNot, allocationPointer
	);
}


__global__ void reduce3NoElementWiseStrideFloat(
		int opNum,
		float *dx,
		int *xShapeInfo,
		float *dy,
		int *yShapeInfo,
		float *extraParams,
		float *result,
		int *resultShapeInfo,
		int postProcessOrNot, int *allocationPointer) {
	reduce3NoElementWiseStrideGeneric<float>(
			opNum,
			dx,
			xShapeInfo,
			dy,
			yShapeInfo,
			extraParams,
			result,
			resultShapeInfo,
			postProcessOrNot, allocationPointer
	);
}

/**
 * The driver api
 * @param opNum the number
 * @param n the length of the reduce
 * @param dx the input data
 * @param xShapeInfo the shape information
 * @param dy the pair wise reduce
 * @param yShapeInfo the shape information for y
 * @param extraParams the extra parameters in the operation
 * @param result where to store the result
 * @param resultShapeInfo the shape information
 * @param gpuInformation the gpu information
 * @param dimension the dimension to reduce along long
 * @param dimensionLength the dimension length
 * @param postProcessOrNot whether to post
 */
template <typename T>
__device__ void reduce3Generic(
		int opNum,
		T *dx,
		int *xShapeInfo,
		T *dy,
		int *yShapeInfo,
		T *extraParams,
		T *result,
		int *resultShapeInfo,
		int *dimension,
		int dimensionLength,
		int postProcessOrNot, int *allocationPointer) {


	__shared__ functions::reduce3::Reduce3<T> * op;
	__shared__ functions::reduce3::Reduce3OpFactory<T> *reduce3OpFactory;

	__shared__ UnifiedSharedMemory<T> *manager;

	if (threadIdx.x == 0) {
		extern __shared__ unsigned char shmem[];
		manager = new(shmem) UnifiedSharedMemory<T>();
		manager->init(sizeof(UnifiedSharedMemory<T>), sizeof(functions::reduce3::Reduce3OpFactory<T>), sizeof(functions::reduce3::Reduce3<T>), sizeof(shape::TAD));
	}
	__syncthreads();

	__shared__ int *ptrSharedXShapeInfo;
	__shared__ int *ptrSharedYShapeInfo;
	__shared__ int *ptrSharedZShapeInfo;

	if (xShapeInfo != nullptr) {
		shape::sweepShapeInfoBuffer(xShapeInfo, manager->getXShapeBuffer());
		if (threadIdx.x == 0) ptrSharedXShapeInfo = manager->getXShapeBuffer();
	} else if (threadIdx.x == 0) ptrSharedXShapeInfo = nullptr;

	if (yShapeInfo != nullptr) {
		shape::sweepShapeInfoBuffer(yShapeInfo, manager->getYShapeBuffer());
		if (threadIdx.x == 0) ptrSharedYShapeInfo = manager->getYShapeBuffer();
	} else if (threadIdx.x == 0) ptrSharedYShapeInfo = nullptr;

	if (resultShapeInfo != nullptr) {
		shape::sweepShapeInfoBuffer(resultShapeInfo, manager->getZShapeBuffer());
		if (threadIdx.x == 0) ptrSharedZShapeInfo = manager->getZShapeBuffer();
	} else if (threadIdx.x == 0) ptrSharedZShapeInfo = nullptr;

	if(threadIdx.x == 0) {
		reduce3OpFactory = new(manager->getFactorySpace()) functions::reduce3::Reduce3OpFactory<T>();
		op = reduce3OpFactory->getOp(opNum, manager->getFunctionSpace());
	}
	__syncthreads();

	op->transform(
			dx,
			ptrSharedXShapeInfo,
			dy,ptrSharedYShapeInfo,
			extraParams,
			result,
			ptrSharedZShapeInfo,
			dimension,
			dimensionLength,
			postProcessOrNot, allocationPointer, manager);
}

/**
 * The driver api
 * @param opNum the number
 * @param n the length of the reduce
 * @param dx the input data
 * @param xShapeInfo the shape information
 * @param dy the pair wise reduce
 * @param yShapeInfo the shape information for y
 * @param extraParams the extra parameters in the operation
 * @param result where to store the result
 * @param resultShapeInfo the shape information
 * @param dimension the dimension to reduce along long
 * @param dimensionLength the dimension length
 * @param postProcessOrNot whether to post [
 */
extern "C"
__global__ void reduce3Double(
		int opNum,
		double *dx,
		int *xShapeInfo,
		double *dy,
		int *yShapeInfo,
		double *extraParams,
		double *result,
		int *resultShapeInfo,
		int *dimension,
		int dimensionLength,
		int postProcessOrNot, int *allocationPointer) {
	reduce3Generic<double>(
			opNum,
			dx,
			xShapeInfo,
			dy,
			yShapeInfo,
			extraParams,
			result,
			resultShapeInfo,
			dimension,
			dimensionLength,
			postProcessOrNot, allocationPointer);

}

/**
 * The driver api
 * @param opNum the number
 * @param n the length of the reduce
 * @param dx the input data
 * @param xShapeInfo the shape information
 * @param dy the pair wise reduce
 * @param yShapeInfo the shape information for y
 * @param extraParams the extra parameters in the operation
 * @param result where to store the result
 * @param resultShapeInfo the shape information
 * @param gpuInformation the gpu information
 * @param dimension the dimension to reduce along long
 * @param dimensionLength the dimension length
 * @param postProcessOrNot whether to post [
 */
extern "C"
__global__ void reduce3Float(
		int opNum,
		float *dx,
		int *xShapeInfo,
		float *dy,
		int *yShapeInfo,
		float *extraParams,
		float *result,
		int *resultShapeInfo,
		int *dimension,
		int dimensionLength,
		int postProcessOrNot, int *allocationPointer) {
	reduce3Generic<float>(
			opNum,
			dx,
			xShapeInfo,
			dy,
			yShapeInfo,
			extraParams,
			result,
			resultShapeInfo,
			dimension,
			dimensionLength,
			postProcessOrNot, allocationPointer);

}

#endif



#endif /* REDUCE3_H_ */
