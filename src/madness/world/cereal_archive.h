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
*/

#ifndef MADNESS_WORLD_CEREAL_ARCHIVE_H__INCLUDED
#define MADNESS_WORLD_CEREAL_ARCHIVE_H__INCLUDED

namespace madness {
template <typename Archive> struct is_cereal_archive;
}

#if __has_include(<cereal/cereal.hpp>)

#ifndef MADNESS_HAS_CEREAL
#define MADNESS_HAS_CEREAL 1
#endif

#include <memory>
#include <cereal/cereal.hpp>
#include <cereal/details/traits.hpp>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>
#include <madness/world/archive.h>
#include <madness/world/parallel_archive.h>

namespace madness {
namespace archive {
/// Wraps an output archive around a Cereal archive
template <typename Muesli>
class CerealOutputArchive : public ::madness::archive::BaseOutputArchive {
  mutable std::shared_ptr<Muesli>
      muesli; ///< The cereal archive being wrapped, deleter determines whether this is an owning ptr

public:
  CerealOutputArchive(Muesli &muesli) : muesli(&muesli, [](Muesli *) {}) {}
  CerealOutputArchive() {}

  template <typename Arg, typename... RestOfArgs,
            typename = std::enable_if_t<
                !std::is_same<Muesli, std::decay_t<Arg>>::value>>
  CerealOutputArchive(Arg &&arg, RestOfArgs &&... rest_of_args)
      : muesli(new Muesli(std::forward<Arg>(arg),
                          std::forward<RestOfArgs>(rest_of_args)...),
               std::default_delete<Muesli>{}) {}

  template <class T, class Cereal = Muesli>
  inline std::enable_if_t<
      madness::is_trivially_serializable<T>::value &&
          !cereal::traits::is_text_archive<Cereal>::value,
      void>
  store(const T *t, long n) const {
    const unsigned char *ptr = (unsigned char *)t;
    (*muesli)(cereal::binary_data(ptr, sizeof(T) * n));
  }

  template <class T, class Cereal = Muesli>
  inline std::enable_if_t<
      !madness::is_trivially_serializable<T>::value ||
          cereal::traits::is_text_archive<Cereal>::value,void>
  store(const T *t, long n) const {
    for (long i = 0; i != n; ++i)
      *muesli & t[i];
  }

  void open(std::size_t hint) {}
  void open(char* buf){}
  void close(){};
  void flush(){};
};

/// Wraps an input archive around a Cereal archive
template <typename Muesli> class CerealInputArchive : public BaseInputArchive {
  std::shared_ptr<Muesli> muesli; ///< The cereal archive being wrapped, deleter determines whether this is an owning ptr

public:
  CerealInputArchive(Muesli &muesli) : muesli(&muesli, [](Muesli *) {}) {}
  template <typename Arg, typename... RestOfArgs,
            typename = std::enable_if_t<
                !std::is_same<Muesli, std::decay_t<Arg>>::value>>
  CerealInputArchive(Arg &&arg, RestOfArgs &&... rest_of_args)
      : muesli(new Muesli(std::forward<Arg>(arg),
                          std::forward<RestOfArgs>(rest_of_args)...),
               std::default_delete<Muesli>{}) {}

  template <class T, class Cereal = Muesli>
  inline std::enable_if_t<
      madness::is_trivially_serializable<T>::value &&
          !cereal::traits::is_text_archive<Cereal>::value,
      void>
  load(T *t, long n) const {
    (*muesli)(cereal::binary_data(t, sizeof(T) * n));
  }

  template <class T, class Cereal = Muesli>
  inline std::enable_if_t<
      !madness::is_trivially_serializable<T>::value ||
          cereal::traits::is_text_archive<Cereal>::value,
      void>
  load(T *t, long n) const {
    for (long i = 0; i != n; ++i)
      *muesli & t[i];
  }

  void open(std::size_t hint) {}
  void rewind() const {}
  void close(){};
};

/// Wraps parallel archive around cereal binary output archives.
class ParallelCerealOutputArchive : public BaseParallelArchive<CerealOutputArchive<cereal::BinaryOutputArchive>>, public BaseOutputArchive {
 public:
     /// Default constructor.
     ParallelCerealOutputArchive() {}

     /// Creates a parallel archive for output with given base filename and number of I/O nodes.

     /// \param[in] world The world.
     /// \param[in] filename Base name of the file.
     /// \param[in] nio The number of I/O nodes.
     ParallelCerealOutputArchive(World& world, const char* filename, int nio=1)  {
         open(world, filename, nio);
     }

     /// Flush any data in the archive.
     void flush() {
         if (is_io_node()) local_archive().flush();
     }
 };
 /// \tparam T The data type. MK
 template <class T>
 struct ArchivePrePostImpl<ParallelCerealOutputArchive,T> {
     /// Store the preamble for this data type in the parallel archive.

     /// \param[in] ar The archive.
     static void preamble_store(const ParallelCerealOutputArchive& ar) {}

     /// Store the postamble for this data type in the parallel archive.

     /// \param[in] ar The archive.
     static inline void postamble_store(const ParallelCerealOutputArchive& ar) {}
 };

 /// Specialization of \c ArchiveImpl for parallel output archives.

 /// \attention No type-checking is performed.
 /// \tparam T The data type.
 template <class T>
 struct ArchiveImpl<ParallelCerealOutputArchive, T> {
     /// Store the data in the archive.

     /// Parallel objects are forwarded to their implementation of parallel store.
     ///
     /// The function only appears (due to \c enable_if) if \c Q is a parallel
     /// serializable object.
     /// \todo Is \c Q necessary? I'm sure it is, but can't figure out why at a first glance.
     /// \tparam Q Description needed.
     /// \param[in] ar The parallel archive.
     /// \param[in] t The parallel object to store.
     /// \return The parallel archive.
     template <typename Q>
     static inline
     typename std::enable_if<std::is_base_of<ParallelSerializableObject, Q>::value, const ParallelCerealOutputArchive&>::type
     wrap_store(const ParallelCerealOutputArchive& ar, const Q& t) {
         ArchiveStoreImpl<ParallelCerealOutputArchive,T>::store(ar,t);
         return ar;
     }

     /// Store the data in the archive.

     /// Serial objects write only from process 0.
     ///
     /// The function only appears (due to \c enable_if) if \c Q is not
     /// a parallel serializable object.
     /// \todo Same question about \c Q.
     /// \tparam Q Description needed.
     /// \param[in] ar The parallel archive.
     /// \param[in] t The serial data.
     /// \return The parallel archive.
     template <typename Q>
     static inline
     typename std::enable_if<!std::is_base_of<ParallelSerializableObject, Q>::value, const ParallelCerealOutputArchive&>::type
     wrap_store(const ParallelCerealOutputArchive& ar, const Q& t) {
         if (ar.get_world()->rank()==0) {
             ar.local_archive() & t;
         }
         return ar;
     }
 };
// Write the archive array only from process zero.

 /// \tparam T The array data type.
 template <class T>
 struct ArchiveImpl< ParallelCerealOutputArchive, archive_array<T> > {
     /// Store the \c archive_array in the parallel archive.

     /// \param[in] ar The parallel archive.
     /// \param[in] t The array to store.
     /// \return The parallel archive.
     static inline const ParallelCerealOutputArchive& wrap_store(const ParallelCerealOutputArchive& ar, const archive_array<T>& t) {
         if (ar.get_world()->rank() == 0) ar.local_archive() & t;
         return ar;
     }
 };

// Forward a fixed-size array to \c archive_array.

 /// \tparam T The array data type.
 /// \tparam n The number of items in the array.
 template <class T, std::size_t n>
 struct ArchiveImpl<ParallelCerealOutputArchive, T[n]> {
     /// Store the array in the parallel archive.

     /// \param[in] ar The parallel archive.
     /// \param[in] t The array to store.
     /// \return The parallel archive.
     static inline const ParallelCerealOutputArchive& wrap_store(const ParallelCerealOutputArchive& ar, const T(&t)[n]) {
         ar << wrap(&t[0],n);
         return ar;
     }
 };

} // namespace archive

template <typename Muesli>
struct is_text_archive<
    archive::CerealInputArchive<Muesli>,
    std::enable_if_t<cereal::traits::is_text_archive<Muesli>::value>>
    : std::true_type {};
template <typename Muesli>
struct is_text_archive<
    archive::CerealOutputArchive<Muesli>,
    std::enable_if_t<cereal::traits::is_text_archive<Muesli>::value>>
    : std::true_type {};

template <typename Muesli, typename T>
struct is_serializable<
    archive::CerealOutputArchive<Muesli>, T,
    std::enable_if_t<(is_trivially_serializable<T>::value &&
        !cereal::traits::is_text_archive<Muesli>::value) ||
        (cereal::traits::detail::count_output_serializers<T, Muesli>::value != 0 &&
         cereal::traits::is_text_archive<Muesli>::value)>>
    : std::true_type {};
template <typename Muesli, typename T>
struct is_serializable<
    archive::CerealInputArchive<Muesli>, T,
    std::enable_if_t<
        (is_trivially_serializable<T>::value &&
            !cereal::traits::is_text_archive<Muesli>::value) ||
            (cereal::traits::detail::count_input_serializers<T, Muesli>::value != 0 &&
             cereal::traits::is_text_archive<Muesli>::value)>>
    : std::true_type {};

template <typename Muesli>
struct is_cereal_archive<archive::CerealOutputArchive<Muesli>> : std::true_type {};
template <typename Muesli>
struct is_cereal_archive<archive::CerealInputArchive<Muesli>> : std::true_type {};

}  // namespace madness

#endif  // have cereal/cereal.hpp

namespace madness {
template <typename Archive> struct is_cereal_archive : std::false_type {};
}

#endif  // MADNESS_WORLD_CEREAL_ARCHIVE_H__INCLUDED
