# M11 live checks pending (cpp_session=1, pair 2)

Prereq: X0 integrated, s2m_session_x0.cpp added to the build, lua_guard.patch applied by the supervisor.

C-rows
- C11 emission follow: fakehost scenario 04_emission_follow.txt. Expect the same log lines as the Lua run: "agenda propria de emissoes em pausa", "host em emissao", a real client emission from the host weather 7, NO XStartEmission, "parada 1 de 3" at most once, saves allowed after.
- C14 time and weather: client with synctime=1 syncweather=1 wxfollow=1: "TIME: XSetGameTime(...) sync gt" when the clocks differ over 600 s, "clima: host em N" + "troca para N confirmada".
- C20 version check: hostcfg=1 on both: "PEER SESSION", "hostcfg: s2core.lua igual nos dois lados", flag adoption lines (difficulty).
- Sleep (N5): host sleeps, client follows (mode 1) or refuses in a dialog; beds blocked while the other is busy; "DORMIMOS JUNTOS".
- No pause (N8): Esc menu on both, "nopause: jogo pausado -> despausado" and the menu usable.

Unmeasured, probe with s2dev (read only)
- x0::prop<uint8_t> on the EnumProperty CurrentWeatherData.TargetWeather (byte at +1 of the struct).
- x0::Call::i on EnumProperty params (XSwitchToWeather/XForceWeather WeatherType) and ByteProperty NewType (SetCollisionEnabled).
- x0::prop<Ref> for BedView.BedMeshComponent.
- Call::rb for GetCollisionEnabled returns a byte: read with ri.
- ftLock / nodeathDead / cineFrozen need M10/M2 interface slots (adapter returns false / story_locked for now): sleep busy states B and X are only partly covered until those exist.
- sleeppull (default 0) needs the M10 pull-now call; the adapter returns false.
- build_md5 / main_ver / core_ver are read as flags by the adapter placeholders; X0 should provide the real values (DLL md5, mod and core version strings).
- Fight bench is not affected by this module (no combat code); run it once after integration to confirm no regression.
