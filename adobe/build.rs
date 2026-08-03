use pipl::*;

// The AE effect API version this plugin was written against.
const PF_PLUG_IN_VERSION: u16 = 13;
const PF_PLUG_IN_SUBVERS: u16 = 28;

#[rustfmt::skip]
fn main() {
    pipl::plugin_build(vec![
        Property::Kind(PIPLType::AEEffect),
        Property::Name("Luma Key"),
        Property::Category("Stoatworks"),

        #[cfg(target_os = "windows")]
        Property::CodeWin64X86("EffectMain"),
        #[cfg(target_os = "macos")]
        Property::CodeMacIntel64("EffectMain"),
        #[cfg(target_os = "macos")]
        Property::CodeMacARM64("EffectMain"),

        Property::AE_PiPL_Version { major: 2, minor: 0 },
        Property::AE_Effect_Spec_Version { major: PF_PLUG_IN_VERSION, minor: PF_PLUG_IN_SUBVERS },
        Property::AE_Effect_Version {
            version: 1,
            subversion: 1,
            bugversion: 1,
            stage: Stage::Release,
            build: 1,
        },
        Property::AE_Effect_Info_Flags(0),
        Property::AE_Effect_Global_OutFlags(
            OutFlags::PixIndependent |
            OutFlags::DeepColorAware
        ),
        Property::AE_Effect_Global_OutFlags_2(
            OutFlags2::SupportsThreadedRendering
        ),
        // The serialisation key. Never change it: compositions refer to it.
        Property::AE_Effect_Match_Name("STWK Luma Key"),
        Property::AE_Reserved_Info(0),
        Property::AE_Effect_Support_URL("https://stoatworks-labs.com/support"),
    ])
}
