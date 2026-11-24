#pragma once

// FieldMesh: Off-Grid radio parameters.
// The Off-Grid frequency is chosen automatically from the normal (pre Off-Grid) frequency,
// so one firmware per device serves both regions:
//   below 900 MHz  -> EU 869.4625 MHz (SRD sub-band g3: 500 mW ERP, 10% duty cycle)
//   900 MHz and up -> US 916.4625 MHz (915 MHz ISM band)
// India (865-867 MHz) maps to EU and Japan/Korea (~920-923 MHz) to US - not compliant there,
// accepted on purpose (see README).

#define OFFGRID_FREQ_EU     869.4625f
#define OFFGRID_FREQ_US     916.4625f
#define OFFGRID_REGION_SPLIT_MHZ  900.0f
#define OFFGRID_BW          125.0f
#define OFFGRID_SF          11
#define OFFGRID_CR          5

static inline bool offGridIsUS(float normal_freq) {
  return normal_freq >= OFFGRID_REGION_SPLIT_MHZ;
}

static inline float offGridFreqFor(float normal_freq) {
  return offGridIsUS(normal_freq) ? OFFGRID_FREQ_US : OFFGRID_FREQ_EU;
}

// region label of an active Off-Grid frequency ("EU" / "US")
static inline const char* offGridRegionLabel(float offgrid_freq) {
  return offGridIsUS(offgrid_freq) ? "US" : "EU";
}

// Radio settings from the app (CMD_SET_RADIO_PARAMS) versus Off-Grid.
// FieldMesh uses the "repeat" switch of the app as its Off-Grid state, and the app always
// sends all radio values together with it.

// Same radio values? The app sends the frequency in whole kHz, so 869.4625 MHz comes back
// rounded: compare with a small tolerance instead of exact floats.
static inline bool offGridSameRadio(float freq_a, float bw_a, int sf_a, int cr_a,
                                    float freq_b, float bw_b, int sf_b, int cr_b) {
  float df = freq_a - freq_b;
  float db = bw_a - bw_b;
  return df > -0.002f && df < 0.002f && db > -0.5f && db < 0.5f && sf_a == sf_b && cr_a == cr_b;
}

// Repeat switched off in the app while Off-Grid is on, with the radio values left as they
// are: the app only meant "Off-Grid off" (or an app without the repeat field saved some
// other radio setting). Leave Off-Grid like the device toggle does, back to the saved normal
// values - otherwise the device shows Off-Grid off but stays on the Off-Grid channel.
// New radio values from the app are taken as they are.
static inline bool offGridAppRestoresNormal(bool offgrid_active, bool app_repeat,
                                            bool same_radio, float normal_freq) {
  return offgrid_active && !app_repeat && same_radio && normal_freq > 0.0f;
}
