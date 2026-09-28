# Display and Refresh

[English](Display_and_Refresh.md) | [Español](es/Display_and_Refresh.md)

The custom driver is a fixed monochrome 800×480 SSD1677 implementation using the PaperMono OTP waveforms. SPI is write-only at 20 MHz. A PARTIAL refresh transfers the complete RAM1 framebuffer; logical dirty regions are currently retained only for logging and diagnostics and do not govern the FULL/PARTIAL decision.

## Policy

- **FULL:** establishes or re-establishes the physical partial baseline.
- **PARTIAL:** updates using the existing baseline.
- **Promotion to physical FULL:** a normal request resolves to FULL whenever the baseline is invalid or its policy threshold has been reached.
- **`RefreshKind::MANDATORY_FULL`:** an explicit request kind used for boot/PMIC recovery and cleanup paths.
- **PMIC recovery gate:** blocks or defers ordinary refresh work until hardware recovery permits the required initial FULL.

A normal refresh request made with an invalid baseline is promoted to a physical FULL; it is not simply discarded as a rejected PARTIAL. Requests made while BUSY are coalesced. A mandatory FULL kind overrides a normal kind; when automatic and user-interaction policies are merged, the automatic policy is retained because it has the lower FULL threshold. Up to four logical partial regions are tracked for diagnostics. Before light sleep, activity waits for both an idle panel and no pending refresh.

The BUSY timeout is 15 seconds. A timeout invalidates the baseline, marks hardware recovery as failed/pending, and logs `BUSY timeout`. Subsequent ordinary refresh requests are blocked. The component has no general automatic retry loop for this condition; recovery requires a reboot or entry into the explicit PMIC hardware-recovery path.

Refreshes can originate from boot, Home Assistant state changes, control actions, periodic wake, or PMIC recovery. The general policy promotes normal requests to FULL when the partial counter reaches 10 for automatic policy or 15 for user-interaction policy. Explicit cleanup paths act earlier: pickup at 8 partials, entry to controls at 8, and exit from controls at 10 request `MANDATORY_FULL`. An invalid baseline promotes a normal request to physical FULL, while boot/PMIC recovery uses the mandatory kind and recovery gate described above. The YAML `full_update_every` option remains in the schema and is set to `0` in `packages/ui.yaml`, but the current policy ignores that value; the central thresholds and explicit cleanup paths still apply.

When external power is present, automatic dashboard refreshes use a separate realtime path. The clock shows the real minute, and minute rollovers mark the dashboard dirty through the same C++ coalescing mechanism as Home Assistant entity updates. The five-minute scheduler refresh is disabled while plugged in, so there is no duplicate refresh at `:00`, `:05`, and so on. Dirty dashboard work debounces for about 1.5 seconds without restart, then refreshes at most about once every 15 seconds while changes remain pending. These refreshes are silent: they do not call `report_activity()`, turn on the frontlight, leave sleep visual state, open controls, or alter notifications. They are skipped while controls, a fullscreen notification, or sleep visual state has priority, but the dirty flag is kept until a refresh can run. The automatic FULL threshold rises to 60 partials while plugged in (~1 FULL/h with one partial/minute from the clock), still read from the PMIC via `is_external_power_present()`. Battery behaviour, including the five-minute scheduler, quantized clock display, periodic wake, and automatic FULL threshold of 10, is unchanged when external power is absent.

Home Assistant supplies the timezone and clock. On battery, the dashboard rounds the displayed minute down to the configured refresh interval—for example, `12:14` is drawn as `12:10` with a five-minute interval. While plugged in, the real minute is shown instead. During a periodic wake timeout, local time and HA values retained in RAM across light sleep are used for the refresh; that cache is not available after a cold boot. A reboot or complete SSD1677 power loss invalidates the old physical baseline, and PMIC recovery performs the mandatory FULL before allowing PARTIAL refreshes.
