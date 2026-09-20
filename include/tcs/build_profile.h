#ifndef TCS_BUILD_PROFILE_H
#define TCS_BUILD_PROFILE_H

/* Include after microkit.h so the SDK's generated kernel flags are visible. */
#if (defined(TCS_DEBUG_PROFILE) + defined(TCS_RELEASE_PROFILE) + defined(TCS_HOST_TEST)) != 1
#error "Select exactly one TCS build profile"
#endif

#if defined(TCS_HOST_TEST)
#if !defined(TCS_TEST_MICROKIT_H) || !__STDC_HOSTED__
#error "TCS_HOST_TEST is restricted to the hosted IPC fixture"
#endif
#define TCS_PROFILE_NAME "host-test"
#elif defined(TCS_RELEASE_PROFILE)
#if defined(CONFIG_DEBUG_BUILD) || defined(CONFIG_PRINTING)
#error "TCS release profile requires a kernel without debug or printing support"
#endif
#if !defined(CONFIG_VERIFICATION_BUILD)
#error "TCS release profile requires the pinned SDK release configuration"
#endif
/* This SDK configuration flag is not a proof or a production-readiness claim. */
#define TCS_PROFILE_NAME "release-kernel"
#else
#if !defined(CONFIG_DEBUG_BUILD) || !defined(CONFIG_PRINTING)
#error "TCS debug profile requires the SDK debug configuration"
#endif
#define TCS_PROFILE_NAME "debug-kernel"
#endif

#endif
