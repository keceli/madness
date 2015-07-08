/*
 * CCOperators.h
 *
 *  Created on: Jul 6, 2015
 *      Author: kottmanj
 */

#ifndef CCOPERATORS_H_
#define CCOPERATORS_H_

// Operators for coupled cluster and CIS

namespace madness {

//#include <chem/SCFOperators.h>

// forward declaration
class SCF;
class Nemo;
class NuclearCorrelationFactor;
class XCfunctional;
class Nuclear;

typedef std::vector<Function<double,3> > vecfuncT;


// Coupled Cluster Operators that use and result in only 3D functions (so CCS and CIS)
class CC_3D_Operator{
public:
//	CC_3D_Operator(World&world, const Nemo &nemo): world(world), mo_ket_(nemo.get_calc() -> amo),R2(init_R2(nemo.nuclear_correlation -> square())){
//		poisson = std::shared_ptr<real_convolution_3d>(CoulombOperatorPtr(world, nemo.get_calc() -> param.lo, nemo.get_calc() ->param.econv));
//		mo_bra_ = mul(world,R2,mo_ket_);
//		exchange_intermediate_ = make_exchange_intermediate();
//		if(mo_bra_.empty()) std::cout << "\n\n!!!!!WARNING: mo_bra_ vector is empty!!!!!\n\n";
//		if(mo_ket_.empty()) std::cout << "\n\n!!!!!WARNING: mo_ket_ vector is empty!!!!!\n\n";
//		//madness::Nuclear U(world,nemo);
//		//nuclear_potential_ =U;
//	}
	// If other mos than the one in the nemo struct are needed (e.g. if lower thresh is demanded -> guess calculations)
	CC_3D_Operator(World&world, const Nemo &nemo,const vecfuncT &mos): world(world), mo_ket_(mos),R2(init_R2(nemo.nuclear_correlation -> square())){
		poisson = std::shared_ptr<real_convolution_3d>(CoulombOperatorPtr(world, nemo.get_calc() -> param.lo,FunctionDefaults<3>::get_thresh()));
		mo_bra_ = mul(world,nemo.nuclear_correlation -> square(),mo_ket_);
		set_thresh(world,mo_bra_,FunctionDefaults<3>::get_thresh());
		set_thresh(world,mo_ket_,FunctionDefaults<3>::get_thresh());
		truncate(world,mo_ket_);
		truncate(world,mo_bra_);
		exchange_intermediate_ = make_exchange_intermediate();
		sanitycheck();
	}

	/// Make shure that R2 gets the right thresh and is constant
	real_function_3d init_R2(const real_function_3d nemo_input )const{
		real_function_3d tmp = copy(nemo_input);
		tmp.set_thresh(FunctionDefaults<3>::get_thresh());
		tmp.truncate();
		tmp.verify();
		return tmp;
	}

	void sanitycheck()const{
		if(mo_ket_.empty()) error("mo_ket_ is empty");
		if(mo_bra_.empty()) error("mo_bra_ is empty");
		if(exchange_intermediate_.empty()) error("exchange intermediate is empty");
		for(auto x: mo_ket_){
			if(x.thresh() != FunctionDefaults<3>::get_thresh()) error("Wrong thresh in mo_ket_ functions");
		}
		for(auto x: mo_bra_){
			if(x.thresh() != FunctionDefaults<3>::get_thresh()) error("Wrong thresh in mo_bra_ functions");
		}
		for(auto tmp: exchange_intermediate_){
			if(tmp.empty()) error("Exchange Intermediate contains empty vectors");
			for(auto x: tmp){
				if(x.thresh() != FunctionDefaults<3>::get_thresh()) error("Wrong thresh in Exchange Intermediate");
			}
		}
		R2.verify();
	}

	void memory_information(const vecfuncT &v, const std::string &msg = "vectorfunction size is: ")const{
		const double x = get_size(world,v);
		if(world.rank()==0) std::cout << msg << "("<< x <<" GB)" << " for " << v.size() <<" functions\n";
	}

	std::vector<vecfuncT> make_exchange_intermediate()const{
		std::vector<vecfuncT> intermediate;
		double memory = 0.0;
		for(size_t i=0;i<mo_bra_.size();i++){
			const vecfuncT integrant = mul(world,mo_bra_[i],mo_ket_);
			const vecfuncT intermediate_i = apply(world,*poisson,integrant);
			intermediate.push_back(intermediate_i);
			memory += get_size(world,intermediate_i);
		}
		if(world.rank()==0) std::cout << "Created exchange intermediate of dimension " << intermediate.size() << "x" << intermediate.front().size() << " and size (" << memory <<   " GB)\n";
		return intermediate;
	}


public:
	// The nuclear potential is missing (or the U potential for the regularized approach)

	// Closed Shell Triplet CIS potential without the nuclear potential
	// returns (2J - K)x + S3C_X
	// which is in components VCIS_j =  2*\sum_i <i|r12|i> |x_j> - \sum_i <i|r12|x_j> |i> - Q\sum_i <i|r12|j> |x_i>
	vecfuncT get_CIS_potential_triplet(const vecfuncT &x)const{
		return add(world,fock_residue_closed_shell(x),S3C_X(x));
	}
	// Closed Shell Singlet CIS potential without the nuclear potential
	// returns (2J - K)x + 2*S3C_C + S3C_X
	// which is in components VCIS_j =  2*\sum_i <i|r12|i> |x_j> - \sum_i <i|r12|x_j> |i> + 2*Q\sum_i <i|r12|x_i> |j> - Q\sum_i <i|r12|j> |x_i>
	vecfuncT get_CIS_potential_singlet(const vecfuncT &x)const{
		vecfuncT S3CC = S3C_C(x);
		scale(world,S3CC,2.0);
		vecfuncT S3CX = S3C_X(x);
		return add(world,fock_residue_closed_shell(x),add(world,S3CX,S3CC));
	}

	// get the ground state density
	real_function_3d make_density()const{
		return make_density(mo_bra_,mo_ket_);
	}

	// Make a density out of two vectorfunctions f and g
	// density = \sum_i |f_i><g_i|
	real_function_3d make_density(const vecfuncT &f, const vecfuncT &g)const{
		if(f.size() != g.size()) error("make_density: sizes of vectors are not equal");
		real_function_3d density = real_factory_3d(world);
		for(size_t i=0;i<f.size();i++) density += f[i]*g[i];
		density.truncate();
		return density;
	}

	// The Fock operator is partitioned into F = T + Vn + R
	// the fock residue R= 2J-K for closed shell is computed here
	// J_j = \sum_i <i|r12|i> |tau>
	// K_j = \sum_i <i|r12|tau_j> |i>
	vecfuncT fock_residue_closed_shell(const vecfuncT &tau)const{
		START_TIMER();
		vecfuncT J = mul(world,(*poisson)(make_density()),tau);
		truncate(world,J);
		scale(world,J,2.0);
		END_TIMER("J");
		START_TIMER();
		vecfuncT K;
		for(size_t j=0;j<tau.size();j++){
			real_function_3d Kj = real_factory_3d(world);
			for(size_t i=0;i<mo_bra_.size();i++){
				Kj += (*poisson)(mo_bra_[i]*tau[j])*mo_ket_[i];
			}
			K.push_back(Kj);
		}
		truncate(world,K);
		scale(world,K,-1);
		END_TIMER("K");
		return add(world,J,K);
	}

	// Diagrammatic Potentials:

	// The coulomb Term of the S3C diagram: Positive sign
	// \     /
	//  \---/  = Q\sum_j(<j|g12|tau_j>)|i>
	//  _\_/_
	vecfuncT S3C_C(const vecfuncT &tau)const{
		START_TIMER();
		vecfuncT result = mul(world,(*poisson)(make_density(mo_bra_,tau)),mo_ket_);
		Q(result);
		truncate(world,result);
		END_TIMER("S3C_C");
		return result;
	}

	// The Exchange Term of the S3C diagram: Negative sign
	// \  /
	//  \/...   = Q\sum_j(<j|g12|i>|tau_j>)
	//     / \
	//    _\_/_
	vecfuncT S3C_X(const vecfuncT &tau)const{
		START_TIMER();
		vecfuncT result;
		for(size_t i=0;i<tau.size();i++){
			real_function_3d tmp= real_factory_3d(world);
			vecfuncT vectmp = mul(world, exchange_intermediate_[i],tau);
			for(size_t j=0;j<tau.size();j++) tmp += vectmp[j];
			tmp.truncate();
			result.push_back(tmp);
		}
		Q(result);
		truncate(world,result);
		scale(world,result,-1.0);
		END_TIMER("S3C_X");
		return result;
	}

	// Project out the occupied space
	void Q(vecfuncT &f)const{
		for(size_t i=0;i<f.size();i++) Q(f[i]);
	}
	void Q(real_function_3d &f)const{
		for(size_t i=0;i<mo_ket_.size();i++){
			f -= mo_bra_[i].inner(f)*mo_ket_[i];
		}
	}

	// Make an inner product between vecfunctions
	double make_inner_product(const vecfuncT &bra, const vecfuncT &ket)const{
		return inner(world,mul(world,R2,bra),ket).sum();
	}
	// inner product between functions
	double make_inner_product(const real_function_3d &bra, const real_function_3d &ket)const{
		return (bra*R2).inner(ket);
	}
	// inner product between function and vecfunction
	double make_inner_product(const real_function_3d &bra, const vecfuncT &ket)const{
		return inner(world,bra*R2,ket).sum();
	}

private:
	bool use_timer_=true;
	World &world;
	vecfuncT mo_bra_,mo_ket_;
	/// The squared nuclear correlation factor;
	const real_function_3d R2;
	std::vector<vecfuncT> exchange_intermediate_;
	std::shared_ptr<real_convolution_3d> poisson;
//	Nuclear nuclear_potential_;
	void error(const std::string &msg)const{
		std::cout << "\n\n\n !!!! ERROR IN CC_3D_OPERATOR CLASS:\n ERROR MESSAGE IS: " << msg <<"\n";
		MADNESS_EXCEPTION("!!!!ERROR IN CC_3D_OPERATOR CLASS!!!!",1);
	}
	// Timer
	mutable double ttt, sss;
	void START_TIMER() const {
		if(use_timer_)world.gop.fence(); ttt=wall_time(); sss=cpu_time();
	}

	void END_TIMER(const std::string msg) const {
		if(use_timer_)END_TIMER(msg.c_str());
	}

	void END_TIMER(const char* msg) const {
		if(use_timer_){
			ttt=wall_time()-ttt; sss=cpu_time()-sss;
			if (world.rank()==0) printf("timer: %20.20s %8.2fs %8.2fs\n", msg, sss, ttt);
		}
	}

};


} /* namespace madness */

#endif /* CCOPERATORS_H_ */
