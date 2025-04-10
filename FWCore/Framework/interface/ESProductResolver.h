// -*- C++ -*-
#ifndef FWCore_Framework_ESProductResolver_h
#define FWCore_Framework_ESProductResolver_h
//
// Package:     Framework
// Class  :     ESProductResolver
//
/**\class edm::eventsetup::ESProductResolver

 Description: Base class for product resolvers held by a EventSetupRecord

 Usage:
    This class defines the interface used to handle retrieving data from an
 EventSetup Record.

*/
//
// Author:      Chris Jones
// Created:     Thu Mar 31 12:43:01 EST 2005
//

// system include files
#include <atomic>

// user include files
#include "FWCore/Utilities/interface/thread_safety_macros.h"
#include "FWCore/Concurrency/interface/WaitingTaskHolder.h"

// forward declarations
namespace edm {
  class EventSetupImpl;
  class ServiceToken;
  class ESParentContext;

  namespace eventsetup {
    struct ComponentDescription;
    class DataKey;
    class EventSetupRecordImpl;

    class ESProductResolver {
    public:
      ESProductResolver();
      ESProductResolver(ESProductResolver const&) = delete;
      ESProductResolver const& operator=(ESProductResolver const&) = delete;
      virtual ~ESProductResolver();

      // ---------- const member functions ---------------------
      bool cacheIsValid() const;

      void prefetchAsync(WaitingTaskHolder,
                         EventSetupRecordImpl const&,
                         DataKey const&,
                         EventSetupImpl const*,
                         ServiceToken const&,
                         ESParentContext const&) const noexcept;

      void const* getAfterPrefetch(const EventSetupRecordImpl& iRecord, const DataKey& iKey, bool iTransiently) const;

      ///returns the description of the ESProductResolverProvider which owns this Resolver
      ComponentDescription const* providerDescription() const { return description_; }

      // ---------- member functions ---------------------------
      void invalidate() {
        clearCacheIsValid();
        invalidateCache();
      }

      void resetIfTransient();

      void setProviderDescription(ComponentDescription const* iDesc) { description_ = iDesc; }

      virtual void initializeForNewIOV() {}

      // Counts setWhatProduced calls if creating module class derives from ESProducer.
      // Currently, all others cases always return 0 (CondDBESSource, unit tests...).
      virtual unsigned int produceMethodID() const;

    protected:
      /**This is the function which does the real work of getting the data if it is not
          already cached.  The returning 'void const*' must point to an instance of the class
          type corresponding to the type designated in iKey. So if iKey refers to a base class interface
          the pointer must be a pointer to that base class interface and not a pointer to an inheriting class
          instance.
          */
      virtual void prefetchAsyncImpl(WaitingTaskHolder,
                                     EventSetupRecordImpl const&,
                                     DataKey const& iKey,
                                     EventSetupImpl const*,
                                     ServiceToken const&,
                                     ESParentContext const&) noexcept = 0;

      /** indicates that the Resolver should invalidate any cached information
          as that information has 'expired' (i.e. we have moved to a new IOV)
          */
      virtual void invalidateCache() = 0;

      /** indicates that the Resolver should invalidate any cached information
          as that information was accessed transiently and therefore is not
          intended to be kept over the entire IOV.  Default is to call
          invalidateCache().
          */
      virtual void invalidateTransientCache();

      /** used to retrieve the data from the implementation. The data is then cached locally.
       */
      virtual void const* getAfterPrefetchImpl() const = 0;

      void clearCacheIsValid();

    private:
      // ---------- member data --------------------------------
      ComponentDescription const* description_;
      mutable std::atomic<void const*> cache_;

      // While implementing the set of code changes that enabled support
      // for concurrent IOVs, I have gone to some effort to maintain
      // the same behavior for this variable and the things that depend on
      // it. My thinking is that we are going to revisit this and make
      // changes in the not so distant future so that the transient feature
      // works again. Alternatively, we may go through and delete it and
      // everything related to it.

      // First comment is that there is only one context in which the value
      // in nonTransientAccessRequested_ is used. This is in the resetIfTransient
      // function. This function is only called immediately after invalidate
      // was called. Therefore the value is always false and condition in
      // resetIfTransient always evaluates true. So in the current code this
      // data member does nothing and has absolutely no purpose. We should
      // delete it and the associated code if we do not modify the code to
      // actually make use of the value stored sometime soon.

      // Currently, this usage occurs is when force cache clear
      // is called from EventProcessor at beginRun (which only happens
      // when a certain configuration parameter is set) and propagates down.
      // It is also used when the looper is trying to start a new loop and
      // calls resetRecordPlusDependentRecords. It is not currently used in
      // any other context.
      //
      // One other thing to note is that the virtual invalidateTransientCache
      // function is defined in this class to just call invalidateCache.
      // Outside of unit tests, the only thing that overrides this definition
      // is in CondCore/ESSources/interface/ESProductResolver.h. So in all other cases
      // the behavior is that invalidateCache is called twice sometimes
      // instead of just once. Possibly it is important that invalidateTransientCache
      // is called in the CondCore code. I don't know.
      mutable std::atomic<bool> nonTransientAccessRequested_;
    };
  }  // namespace eventsetup
}  // namespace edm
#endif
