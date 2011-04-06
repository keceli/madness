/*
  This file is part of MADNESS.

  Copyright (C) 2007,2010 Oak Ridge National Laboratory

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

  For more information please contact:

  Robert J. Harrison
  Oak Ridge National Laboratory
  One Bethel Valley Road
  P.O. Box 2008, MS-6367

  email: harrisonrj@ornl.gov
  tel:   865-241-3937
  fax:   865-572-0680

  $Id$
*/


#ifndef MADNESS_MRA_FUNCTION_FACTORY_AND_INTERFACE_H__INCLUDED
#define MADNESS_MRA_FUNCTION_FACTORY_AND_INTERFACE_H__INCLUDED

#include <tensor/tensor.h>
#include <mra/convolution1d.h>
#include <mra/key.h>
#include <mra/funcdefaults.h>
#include "mra/electron_repulsion.h"
#include "mra/sepreptensor.h"
#include "world/print.h"


/// Holds machinery to set up Functions/FuncImpls using various Factories and Interfaces

/// We provide an abstract base class FunctionFunctorInterface, of which we derive
/// (as of now) the following classes:
///  - ElementaryInterface (formerly FunctorInterfaceWrapper) to wrap elementary functions
///  - FunctionInterface to wrap a FuncImpl (TBD)
///  - ElectronRepulsionInterface to provide 1/r12, which is not elementarily accessible
///  - CompositeFunctionInterface to provide on-demand coefficients of pair functions
///
/// Each of these Interfaces can be used in a FunctionFactory to set up a Function



namespace madness {

	template<typename T, std::size_t NDIM>
	class FunctionImpl;

	template<typename T, std::size_t NDIM>
	class FunctionNode;

    template<typename T, std::size_t NDIM>
    class FunctionCommonData;

    template<typename T, std::size_t NDIM>
    class FunctionFactory;

    template<typename T, std::size_t NDIM, std::size_t MDIM>
    class CompositeFactory;
}


namespace madness {

	/// Abstract base class interface required for functors used as input to Functions
	template<typename T, std::size_t NDIM>
	class FunctionFunctorInterface {
	public:

		typedef Tensor<T> tensorT;
		typedef GenTensor<T> coeffT;
		typedef Key<NDIM> keyT;

		/// You should implement this to return \c f(x)
		virtual T operator()(const Vector<double, NDIM>& x) const = 0;

		/// Override this to return list of special points to be refined more deeply
		virtual std::vector< Vector<double,NDIM> > special_points() const {
			return std::vector< Vector<double,NDIM> >();
		}

		/// Override this change level refinement for special points (default is 6)
		virtual Level special_level() {return 6;}

		virtual ~FunctionFunctorInterface() {}

		virtual coeffT coeff(const keyT&, const bool NS=false) {
			MADNESS_EXCEPTION("implement coeff for FunctionFunctorInterface",0);
		}

		/// does this functor directly provide sum coefficients? or only function values?
		virtual bool provides_coeff() const {
			return false;
		}

		/// override this to return the muster tree
		virtual std::shared_ptr< FunctionImpl<T,NDIM> > get_muster() const {
			MADNESS_EXCEPTION("no method get_muster provided for this FunctionFunctorInterface",0);
		}

	};


	/// CompositeFunctorInterface implements a wrapper of holding several functions and functors

	/// Use this to "connect" several functions and/or functors and to return their coefficients
	/// e.g. connect f1 and f2 with an addition, you can request the coefficients of any node
	/// and they will be computed on the fly and returned. Mainly useful to connect a functor
	/// with a function, if the functor is too large to be represented in MRA (e.g. 1/r12)
	///
	/// as of now, the operation connecting the functions/functors is simply addition.
	/// need to implement expression templates, if I only knew what that was...
	template<typename T, std::size_t NDIM, std::size_t MDIM>
	class CompositeFunctorInterface : public FunctionFunctorInterface<T,NDIM> {

	public:
		typedef FunctionNode<T,NDIM> nodeT;
		typedef GenTensor<T> coeffT;
		typedef Tensor<T> tensorT;
		typedef Vector<double, NDIM> coordT; ///< Type of vector holding coordinates
		typedef Key<NDIM> keyN;
		typedef Key<MDIM> keyM;
		typedef WorldContainer<keyN,nodeT> dcT; ///< Type of container holding the coefficients

	private:

		/// various MRA functions of NDIM dimensionality
		std::shared_ptr< FunctionImpl<T,NDIM> > impl_ket;	///< supposedly the pair function
		std::shared_ptr< FunctionImpl<T,NDIM> > impl_eri;	///< supposedly 1/r12

		/// various MRA functions of MDIM dimensionality (e.g. 3, if NDIM==6)
		std::shared_ptr< FunctionImpl<T,MDIM> > impl_m1;	///< supposedly 1/r1
		std::shared_ptr< FunctionImpl<T,MDIM> > impl_m2;	///< supposedly 1/r2

		/// caching this
		std::shared_ptr< FunctionImpl<T,NDIM> > this_impl;

		/// muster tree for the result
		std::shared_ptr< FunctionImpl<T,NDIM> > muster_impl;

		/// function of order 2k
		std::shared_ptr< FunctionImpl<T,NDIM> > impl_nk;


		/// common data
		const FunctionCommonData<T,NDIM>& cdataN;
		const FunctionCommonData<T,MDIM>& cdataM;

		static const int nk=2;

	public:

		/// constructor takes its Factory
		CompositeFunctorInterface(const CompositeFactory<T,NDIM,MDIM>& factory)
			: impl_ket(factory._ket)
			, impl_eri(factory._g12)
			, impl_m1(factory._v1)
			, impl_m2(factory._v2)
			, this_impl()
			, muster_impl(factory._tree)
//			, this_impl(new FunctionImpl<T,NDIM>(static_cast<const FunctionFactory<T,NDIM>& > (factory)))
			, cdataN(FunctionCommonData<T,NDIM>::get(factory.get_k()))
			, cdataM(FunctionCommonData<T,MDIM>::get(factory.get_k()))
		{


			// some consistency checks

			// either a pair ket is provided, or two particles (tba)
			MADNESS_ASSERT(impl_ket);

			// initialize this_impl; need to remove the functor to avoid an infinite loop
			FunctionFactory<T,NDIM> this_factory(static_cast<const FunctionFactory<T,NDIM>& > (factory));
			this_impl=std::shared_ptr< FunctionImpl<T,NDIM> >(new FunctionImpl<T,NDIM>(this_factory.no_functor()));


			// initialize auxiliary function of polynomial order 2k to call its member functions
			impl_nk=std::shared_ptr< FunctionImpl<T,NDIM> >
						(new FunctionImpl<T,NDIM>(this_factory.no_functor().k(nk*this_impl->get_k())));

			// prepare base functions that make this function
			if (not impl_ket->is_on_demand()) impl_ket->make_redundant(true);
			if (impl_eri) {
				if (not impl_eri->is_on_demand()) impl_eri->make_redundant(true);
			}
			if (impl_m1) {
				if (not impl_m1->is_on_demand()) impl_m1->make_redundant(true);
				if (not impl_m2->is_on_demand()) impl_m2->make_redundant(true);
			}

		}


		/// return value at point x; fairly inefficient
		T operator()(const coordT& x) const {
			print("there is no operator()(coordT&) in CompositeFunctorInterface, for good reason");
			MADNESS_ASSERT(0);
			return T(0);
		};

		bool provides_coeff() const {
			return true;
		}

		/// return sum coefficients for imagined node at key

		/// @param[in]	key	of the Node of which the coeffs are requested
		/// @param[in]	NS	if true return NS form, if false return only sum
		/// @return 	sum coeffs of NS coeffs
		coeffT coeff(const Key<NDIM>& key, const bool NS=false)  {

			coeffT c;

			// try to find coeffs
			dcT& coeffs=this_impl->get_coeffs();
			typename dcT::const_iterator end = coeffs.end();
			const typename dcT::const_iterator it=coeffs.find(key).get();

			bool found=false;
			if (it!=coeffs.end()) {	// maybe we found sth
				if (NS) found=(it->second.coeff().dim(0)==2*this_impl->get_k());
				if (!NS) found=(it->second.coeff().dim(0)==this_impl->get_k());
			}

			if (not found) {

				// make NS coeff
				if (NS) {
					c = coeffT(cdataN.v2k,FunctionDefaults<NDIM>::get_tensor_type());
					for (KeyChildIterator<NDIM> kit(key); kit; ++kit) {
						const keyN& child = kit.key();
						c(this_impl->child_patch(child)) += this->coeff(child,false);
					}
					c = this_impl->filter(c);
					c.reduceRank(this_impl->get_thresh());

				} else {	// make sum coeff
					tensorT cc=make_coeff(key);
					c=coeffT(cc,FunctionDefaults<NDIM>::get_thresh(),FunctionDefaults<NDIM>::get_tensor_type());
				}

			} else {	// found something
				c=it->second.coeff();
			}

			if (NS) {
				MADNESS_ASSERT(c.dim(0)==2*this_impl->get_k());
			}
			return c;
		}


		/// return sum coefficients for imagined node at key
		tensorT make_coeff(const Key<NDIM>& key) {

			tensorT coeff_ket(cdataN.vk);	// 6D, to be multiplied
			tensorT coeff_2p(cdataN.vk);	// 6D, to be added
			tensorT coeff_m1(cdataM.vk);	// 3D, particle 1
			tensorT coeff_m2(cdataM.vk);	// 3D, particle 2
			tensorT coeff_v(cdataN.vk);		// 6D, all addends

			// break key into particles
			const Vector<Translation, NDIM> l=key.translation();
			const Vector<Translation, MDIM> l1=Vector<Translation,MDIM> (vec(l[0],l[1],l[2]));
			const Vector<Translation, MDIM> l2=Vector<Translation,MDIM> (vec(l[3],l[4],l[5]));
			Key<MDIM> key1(key.level(),l1);
			Key<MDIM> key2(key.level(),l2);

			// get the coefficients
			if (impl_ket) coeff_ket=impl_ket->demand_coeffs(key);
			if (impl_eri) coeff_2p=impl_eri->demand_coeffs(key);
			if (impl_m1) coeff_m1=impl_m1->demand_coeffs(key1);
			if (impl_m2) coeff_m2=impl_m2->demand_coeffs(key2);

			if (impl_m1) {
				// extend 3D particles to 6D and add them
				Tensor<double> unity=Tensor<double>(cdataM.vk,false);
				unity=1.0;
				MADNESS_ASSERT(impl_m1 and impl_m2);

				// transform to function values
				tensorT val11=impl_m1->coeffs2values(key1,coeff_m1);
				tensorT val22=impl_m2->coeffs2values(key2,coeff_m2);

				// direct product: V(1) * E(2) + E(1) * V(2)
				coeff_v = outer(val11,unity) + outer(unity,val22);
				coeff_v = this_impl->values2coeffs(key,coeff_v);
			}

			// add 2-particle contribution
			if (impl_eri) coeff_v+=coeff_2p;


			// multiply potential with ket
			if (impl_m1 or impl_eri) {
#if 1
				// project into a 2k basis
				std::vector<long> vnk(NDIM,nk*this_impl->get_k());
                tensorT c1(vnk);
                tensorT c2(vnk);
                std::vector<Slice> s(cdataN.s0);
                c1(s) = coeff_ket(s);
                c2(s) = coeff_v(s);

                tensorT val1= (impl_nk->coeffs2values(key,c1));
                tensorT val2= (impl_nk->coeffs2values(key,c2));

//				tensorT tcube(cdataN.v2k,false);
				tensorT tcube(vnk,false);
				TERNARY_OPTIMIZED_ITERATOR(T, tcube, T, val1, T, val2, *_p0 = *_p1 * *_p2;);
				c2=impl_nk->values2coeffs(key,tcube);
				coeff_ket=c2(s);


#else
				tensorT val1= (impl_ket->coeffs2values(key,coeff_ket));
				tensorT val2= (this_impl->coeffs2values(key,coeff_v));

				tensorT tcube(cdataN.vk,false);
				TERNARY_OPTIMIZED_ITERATOR(T, tcube, T, val1, T, val2, *_p0 = *_p1 * *_p2;);
				coeff_ket=impl_ket->values2coeffs(key,tcube);
#endif
			}

			return coeff_ket;
//			coeffT coeff=coeffT(coeff_ket,FunctionDefaults<NDIM>::get_thresh(),FunctionDefaults<NDIM>::get_tensor_type());

//			return coeff;
		};

		/// return the muster tree
		std::shared_ptr< FunctionImpl<T,NDIM> > get_muster() const {
			return muster_impl;
		}


		/// return the tree keeping the precomputed nodes
//		std::shared_ptr< FunctionImpl<T,NDIM> > get_precomputed() const {
//			return this_impl;
//		}
	};


	/// ElementaryInterface (formerly FunctorInterfaceWrapper) interfaces a c-function

	/// hard-code your favorite function and interface it with this; Does only
	/// provide function values, no MRA coefficients. Care must be taken if the
	/// function we refer to is a singular function, and a on-demand function
	/// at the same time, since direct computation of coefficients via mraimpl::project
	/// might suffer from inaccurate quadrature.
	template<typename T, std::size_t NDIM>
	class ElementaryInterface : public FunctionFunctorInterface<T,NDIM> {

	public:
		typedef Vector<double, NDIM> coordT; ///< Type of vector holding coordinates

		T (*f)(const coordT&);

		ElementaryInterface(T (*f)(const coordT&)) : f(f) {}

		T operator()(const coordT& x) const {return f(x);}
	};


	/// ElectronRepulsionInterface implements the electron repulsion term 1/r12

	/// this is essentially just a wrapper around ElectronRepulsion
	template<typename T, std::size_t NDIM>
	class ElectronRepulsionInterface : public FunctionFunctorInterface<T,NDIM> {

	public:
		typedef GenTensor<T> coeffT;
		typedef Tensor<T> tensorT;
		typedef Vector<double, NDIM> coordT; ///< Type of vector holding coordinates
//		typedef Key<NDIM> keyN;

	private:

		/// the class computing the coefficients
		ElectronRepulsion<NDIM> eri;


	public:

		/// constructor takes the same parameters as the Coulomb operator
		/// which it uses to compute the coefficients
		ElectronRepulsionInterface(World& world,double lo,double eps,
                const BoundaryConditions<NDIM>& bc=FunctionDefaults<NDIM>::get_bc(),
                int k=FunctionDefaults<NDIM>::get_k())
			: eri(ElectronRepulsion<NDIM>(world,eps,eps,bc,k)) {
		}

		bool provides_coeff() const {
			return true;
		}


		/// return value at point x; fairly inefficient
		T operator()(const coordT& x) const {
			print("there is no operator()(coordT&) in ElectronRepulsionInterface, for good reason");
			MADNESS_ASSERT(0);
			return T(0);
		};


		/// return sum coefficients for imagined node at key
		coeffT coeff(const Key<NDIM>& key, const bool NS=false)  {
			MADNESS_ASSERT(not NS);
			return coeffT(this->eri.coeff(key),FunctionDefaults<NDIM>::get_thresh(),FunctionDefaults<NDIM>::get_tensor_type());
		}

	};



	/// FunctionFactory implements the named-parameter idiom for Function

	/// C++ does not provide named arguments (as does, e.g., Python).
	/// This class provides something very close.  Create functions as follows
	/// \code
	/// double myfunc(const double x[]);
	/// Function<double,3> f = FunctionFactory<double,3>(world).f(myfunc).k(11).thresh(1e-9).debug()
	/// \endcode
	/// where the methods of function factory, which specify the non-default
	/// arguments eventually passed to the \c Function constructor, can be
	/// used in any order.
	///
	/// Need to add a general functor for initial projection with a standard interface.
	template<typename T, std::size_t NDIM>
	class FunctionFactory {
		friend class FunctionImpl<T, NDIM> ;
		typedef Vector<double, NDIM> coordT; ///< Type of vector holding coordinates
	protected:
		World& _world;
		int _k;
		double _thresh;
		int _initial_level;
		int _max_refine_level;
		int _truncate_mode;
		bool _refine;
		bool _empty;
		bool _autorefine;
		bool _truncate_on_project;
		bool _fence;
		bool _is_on_demand;
		//Tensor<int> _bc;
		std::shared_ptr<WorldDCPmapInterface<Key<NDIM> > > _pmap;

	private:
		// need to keep this private, access only via get_functor();
		// reason is that the functor must only be constructed when the actual
		// FuncImpl is constructed, otherwise we might depend on the ordering
		// of the chaining (specifically, if the functor is constructed before
		// of after the threshold is changed)
		std::shared_ptr<FunctionFunctorInterface<T, NDIM> > _functor;

	public:

		FunctionFactory(World& world) :
				_world(world),
				_k(FunctionDefaults<NDIM>::get_k()),
				_thresh(FunctionDefaults<NDIM>::get_thresh()),
				_initial_level(
					FunctionDefaults<NDIM>::get_initial_level()),
				_max_refine_level(
					FunctionDefaults<NDIM>::get_max_refine_level()),
				_truncate_mode(
					FunctionDefaults<NDIM>::get_truncate_mode()),
				_refine(FunctionDefaults<NDIM>::get_refine()),
				_empty(false),
				_autorefine(FunctionDefaults<NDIM>::get_autorefine()),
				_truncate_on_project(
					FunctionDefaults<NDIM>::get_truncate_on_project()),
				_fence(true), // _bc(FunctionDefaults<NDIM>::get_bc()),
				_is_on_demand(false),
				_pmap(FunctionDefaults<NDIM>::get_pmap()), _functor() {
		}
		virtual ~FunctionFactory() {};
		FunctionFactory&
		functor(
			const std::shared_ptr<FunctionFunctorInterface<T, NDIM> >& f) {
			_functor = f;
			return *this;
		}
		FunctionFactory&
		no_functor() {
			_functor.reset();
			return *this;
		}
		FunctionFactory&
		f(T
		  (*f)(const coordT&)) {
			functor(std::shared_ptr<ElementaryInterface<T, NDIM> > (
						new ElementaryInterface<T,NDIM>(f)));
			return *this;
		}
		virtual FunctionFactory&
		k(int k) {
			_k = k;
			return *this;
		}
		virtual FunctionFactory&
		thresh(double thresh) {
			_thresh = thresh;
			return *this;
		}
		FunctionFactory&
		initial_level(int initial_level) {
			_initial_level = initial_level;
			return *this;
		}
		FunctionFactory&
		max_refine_level(int max_refine_level) {
			_max_refine_level = max_refine_level;
			return *this;
		}
		FunctionFactory&
		truncate_mode(int truncate_mode) {
			_truncate_mode = truncate_mode;
			return *this;
		}
		FunctionFactory&
		refine(bool refine = true) {
			_refine = refine;
			return *this;
		}
		FunctionFactory&
		norefine(bool norefine = true) {
			_refine = !norefine;
			return *this;
		}
		FunctionFactory&
		empty() {
			_empty = true;
			return *this;
		}
		FunctionFactory&
		autorefine() {
			_autorefine = true;
			return *this;
		}
		FunctionFactory&
		noautorefine() {
			_autorefine = false;
			return *this;
		}
		FunctionFactory&
		truncate_on_project() {
			_truncate_on_project = true;
			return *this;
		}
		FunctionFactory&
		notruncate_on_project() {
			_truncate_on_project = false;
			return *this;
		}
		FunctionFactory&
		fence(bool fence = true) {
			_fence = fence;
			return *this;
		}
		FunctionFactory&
		nofence() {
			_fence = false;
			return *this;
		}
		virtual FunctionFactory&
		is_on_demand() {
			_is_on_demand = true;
			return *this;
		}
		FunctionFactory&
		pmap(const std::shared_ptr<WorldDCPmapInterface<Key<NDIM> > >& pmap) {
			_pmap = pmap;
			return *this;
		}

		int get_k() const {return _k;};
		double get_thresh() const {return _thresh;};
		World& get_world() const {return _world;};

		/// return the functor; override this if the functor needs deferred construction
		virtual std::shared_ptr<FunctionFunctorInterface<T, NDIM> > get_functor() const {
    		return _functor;
    	}
	};


    /// Factory for facile setup of a CompositeFunctorInterface and its FuncImpl

    /// for the concept of a Factory see base class FunctionFactory
    /// here we need to provide two different dimensions, since the main purpose
    /// of this is to set up a pair function (6D), consisting of orbitals (3D),
    /// and also one- and two-electron potentials
	///
	/// This Factory constructs a FuncImpl, and also the functor to it.
    template<typename T, std::size_t NDIM, std::size_t MDIM>
    class CompositeFactory : public FunctionFactory<T, NDIM> {
    public:
    	std::shared_ptr<FunctionImpl<T,NDIM> > _ket;		///< supposedly a 6D pair function ket
    	std::shared_ptr<FunctionImpl<T,NDIM> > _g12;		///< supposedly a interaction potential
    	std::shared_ptr<FunctionImpl<T,MDIM> > _v1; 		///< supposedly a potential for particle 1
    	std::shared_ptr<FunctionImpl<T,MDIM> > _v2; 		///< supposedly a potential for particle 2
    	std::shared_ptr<FunctionImpl<T,MDIM> > _particle1; 	///< supposedly particle 1
    	std::shared_ptr<FunctionImpl<T,MDIM> > _particle2; 	///< supposedly particle 2
    	std::shared_ptr<FunctionImpl<T,NDIM> > _tree;	 	///< future tree structure

    private:
    	std::shared_ptr<CompositeFunctorInterface<T, NDIM, MDIM> > _func;

    	friend class CompositeFunctorInterface<T, NDIM, MDIM>;

    public:
    	CompositeFactory(World& world)
    		: FunctionFactory<T,NDIM>(world)
    		, _ket()
    		, _g12()
    		, _v1()
    		, _v2()
    		, _particle1()
			, _particle2()
			, _tree()
			, _func() {

    		// there are certain defaults that make only sense here
    		this->_is_on_demand=true;
    	}


    	/// provide directly the NDIM (6D) pair function ket
        CompositeFactory&
        ket(const std::shared_ptr<FunctionImpl<T, NDIM> >& f) {
        	_ket = f;
        	return *this;
        }

        /// g12 is the interaction potential (6D)
        CompositeFactory&
        g12(const std::shared_ptr<FunctionImpl<T, NDIM> >& f) {
        	_g12 = f;
        	return *this;
        }

        /// a one-particle potential, acting on particle 1
        CompositeFactory&
        V_for_particle1(const std::shared_ptr<FunctionImpl<T, MDIM> >& f) {
        	_v1 = f;
        	return *this;
        }

        /// a one-particle potential, acting on particle 2
        CompositeFactory&
        V_for_particle2(const std::shared_ptr<FunctionImpl<T, MDIM> >& f) {
        	_v2 = f;
        	return *this;
        }

        /// provide particle 1, used with particle 2 to set up a pair function by
        /// direct product
        CompositeFactory&
        particle1(const std::shared_ptr<FunctionImpl<T, MDIM> >& f) {
        	print("particle 1 option ignored for the time being in CompositeFactory");
        	_particle1 = f;
        	return *this;
        }

        /// provide particle 2, used with particle 1 to set up a pair function by
        /// direct product
        CompositeFactory&
        particle2(const std::shared_ptr<FunctionImpl<T, MDIM> >& f) {
        	print("particle 2 option ignored for the time being in CompositeFactory");
        	_particle2 = f;
        	return *this;
        }

        /// provide a function as tree structure
        CompositeFactory&
        muster(const std::shared_ptr<FunctionImpl<T, NDIM> >& f) {
        	print("tree structure set in CompositeFactory");
        	_tree=f;
        	return *this;
        }

        CompositeFactory&
		thresh(double thresh) {
			this->_thresh = thresh;
			return *this;
		}

    	// access to the functor *only* via this
    	std::shared_ptr<FunctionFunctorInterface<T, NDIM> > get_functor() const {

    		// return if we already have a valid functor
    		if (this->_func) return this->_func;

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		// this Factory not only constructs the Function, but also the functor, so
    		// pass *this to the interface
    		const_cast< std::shared_ptr<CompositeFunctorInterface<T, NDIM, MDIM> >& >(this->_func)=
    				std::shared_ptr<CompositeFunctorInterface<T, NDIM, MDIM> >(
    				new CompositeFunctorInterface<double, NDIM, MDIM>(*this));

    		return this->_func;
    	}



    };


    /// Factory to set up an ElectronRepulsion Function
    template<typename T, std::size_t NDIM>
    class ERIFactory : public FunctionFactory<T, NDIM> {

    private:
    	std::shared_ptr<ElectronRepulsionInterface<T, NDIM> > _eri;

    public:

    	/// cutoff radius for 1/r12, aka regularization
    	double _dcut;
        BoundaryConditions<NDIM> _bc;
        int _k;

    public:
    	ERIFactory(World& world)
    		: FunctionFactory<T,NDIM>(world)
    		, _eri()
    		, _dcut(FunctionDefaults<NDIM>::get_thresh())
    		, _bc(FunctionDefaults<NDIM>::get_bc())
    		, _k(FunctionDefaults<NDIM>::get_k())
    	{
    		this->_is_on_demand=true;
    		MADNESS_ASSERT(NDIM==6);
    	}

    	ERIFactory&
		thresh(double thresh) {
			this->_thresh = thresh;
			return *this;
		}

    	ERIFactory&
		dcut(double dcut) {
			this->_dcut = dcut;
			return *this;
		}

    	ERIFactory&
		k(double k) {
			this->_k = k;
			return *this;
		}

    	// access to the functor *only* via this
    	std::shared_ptr<FunctionFunctorInterface<T, NDIM> > get_functor() const {

    		// return if we already have a valid eri
    		if (this->_eri) return this->_eri;

    		print("set dcut in ERIFactory to ", _dcut);

    		// construction of the functor is const in spirit, but non-const in sad reality..
    		const_cast< std::shared_ptr<ElectronRepulsionInterface<T, NDIM> >& >(this->_eri)=
    				std::shared_ptr<ElectronRepulsionInterface<T, NDIM> >(
    				new ElectronRepulsionInterface<double,NDIM>(this->_world,_dcut,this->_thresh,
    		                _bc,_k));

    		return this->_eri;
    	}

    };


}

#endif // MADNESS_MRA_FUNCTION_FACTORY_AND_INTERFACE_H__INCLUDED
