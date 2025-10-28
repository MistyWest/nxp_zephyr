/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fsl_spc.h>
#include <soc.h>
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <zephyr/cpu_freq/cpu_freq.h>
#include <zephyr/cpu_freq/pstate.h>

LOG_MODULE_REGISTER(mcxn_cpu_freq, CONFIG_CPU_FREQ_LOG_LEVEL);

static void mcxn_set_cpu_frequency_to_150mhz(void);
static void mcxn_set_cpu_frequency_to_48mhz(void);
static void mcxn_set_cpu_frequency_to_12mhz(void);

/* MCXN SoC specific P-state configuration. Each P-state node in
 * the devicetree provides a CPU frequency mode (low/medium/high),
 * which we map to a concrete CPU frequency at runtime.
 */
struct mcxn_pstate_cfg {
	uint8_t mode_idx; /* 0: low, 1: medium, 2: high (enum order) */
};

/* Map devicetree performance-states into pstate instances visible to the
 * CPUFreq policy. The generic pstate struct carries a vendor-specific
 * configuration pointer which we populate with mcxn_pstate_cfg.
 */
#define DEFINE_MCXN_PSTATE(node_id)							\
	static const struct mcxn_pstate_cfg _CONCAT(mcxn_pstate_cfg_, node_id) = {	\
			.mode_idx = DT_ENUM_IDX(node_id, cpu_frequency_level),		\
		};									\
	PSTATE_DT_DEFINE(node_id, &_CONCAT(mcxn_pstate_cfg_, node_id))

DT_FOREACH_CHILD_STATUS_OKAY(DT_PATH(performance_states), DEFINE_MCXN_PSTATE)

/* Clock switching routine. On MCXN23x, the clock sources of peripherals do not
 * come from the main clock, so frequency modulation will not affect the operation
 * of peripherals.
 */
static int mcxn_set_cpu_frequency(uint32_t target_hz)
{
	switch (target_hz) {
	case 150000000U:
		LOG_DBG("Switching CPU clock to 150 MHz");
		mcxn_set_cpu_frequency_to_150mhz();
		return 0;
	case 48000000U:
		LOG_DBG("Switching CPU clock to 48 MHz");
		mcxn_set_cpu_frequency_to_48mhz();
		return 0;
	case 12000000U:
		LOG_DBG("Switching CPU clock to 12 MHz");
		mcxn_set_cpu_frequency_to_12mhz();
		return 0;
	default:
		LOG_ERR("Unsupported CPU frequency: %u Hz", target_hz);
		return -ENOTSUP;
	}
}

int cpu_freq_pstate_set(const struct pstate *state)
{
	if (state == NULL) {
		LOG_ERR("P-state is NULL");
		return -EINVAL;
	}

	const struct mcxn_pstate_cfg *cfg = (const struct mcxn_pstate_cfg *)state->config;

	if (cfg == NULL) {
		LOG_ERR("P-state vendor config is NULL");
		return -EINVAL;
	}

	/* Map mode index to concrete frequency in Hz (0=low,1=medium,2=high). */
	uint32_t target_hz;

	switch (cfg->mode_idx) {
	case 2U: /* high */
		target_hz = 150000000U;
		break;
	case 1U: /* medium */
		target_hz = 48000000U;
		break;
	case 0U: /* low */
	default:
		target_hz = 12000000U;
		break;
	}

	LOG_DBG("Requesting CPU frequency change: mode_idx=%u -> %u Hz (threshold=%u%%)",
			cfg->mode_idx, target_hz, state->load_threshold);

	return mcxn_set_cpu_frequency(target_hz);
}

static void mcxn_set_cpu_frequency_to_150mhz(void)
{
	/* Switch to FRO 12M first to ensure we can change the clock setting */
	CLOCK_AttachClk(kFRO12M_to_MAIN_CLK);

	/* Set the DCDC VDD regulator to 1.2 V voltage level */
	spc_active_mode_dcdc_option_t dcdc_cfg = {
		.DCDCVoltage	 = kSPC_DCDC_OverdriveVoltage,
		.DCDCDriveStrength = kSPC_DCDC_NormalDriveStrength,
	};
	SPC_SetActiveModeDCDCRegulatorConfig(SPC0, &dcdc_cfg);

	/* Set the LDO_CORE VDD regulator to 1.2 V voltage level */
	spc_active_mode_core_ldo_option_t ldo_cfg = {
		.CoreLDOVoltage	 = kSPC_CoreLDO_OverDriveVoltage,
		.CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength,
	};
	SPC_SetActiveModeCoreLDORegulatorConfig(SPC0, &ldo_cfg);

	/* Configure Flash wait-states to support 1.2V voltage level and 15MHz frequency */;
	FMU0->FCTRL = (FMU0->FCTRL & ~((uint32_t)FMU_FCTRL_RWSC_MASK)) | (FMU_FCTRL_RWSC(0x3U));

	/* Specifies the 1.2V operating voltage for the SRAM's read/write timing margin */
	spc_sram_voltage_config_t sram_cfg = {
		.operateVoltage	 = kSPC_sramOperateAt1P2V,
		.requestVoltageUpdate = true,
	};
	SPC_SetSRAMOperateVoltage(SPC0, &sram_cfg);

	/*!< Set up PLL0 to 150MHz */
	const pll_setup_t pll0_cfg = {
		.pllctrl = SCG_APLLCTRL_SOURCE(1U) | SCG_APLLCTRL_SELI(27U) |
			SCG_APLLCTRL_SELP(13U),
		.pllndiv = SCG_APLLNDIV_NDIV(8U),
		.pllpdiv = SCG_APLLPDIV_PDIV(1U),
		.pllmdiv = SCG_APLLMDIV_MDIV(50U),
		.pllRate = 150000000U
	};
	CLOCK_SetPLL0Freq(&pll0_cfg);
	CLOCK_SetPll0MonitorMode(kSCG_Pll0MonitorDisable);

	/* Attach PLL0 (150MHz) to MainClock, set clock divider to 1 */
	CLOCK_AttachClk(kPLL0_to_MAIN_CLK);
	CLOCK_SetClkDiv(kCLOCK_DivAhbClk, 1U);
}

static void mcxn_set_cpu_frequency_to_48mhz(void)
{
	/* Switch to FRO 12M first to ensure we can change the clock setting */
	CLOCK_AttachClk(kFRO12M_to_MAIN_CLK);

	/* Set the LDO_CORE VDD regulator to 1.0 V voltage level */
	spc_active_mode_core_ldo_option_t ldo_cfg = {
		.CoreLDOVoltage	 = kSPC_CoreLDO_MidDriveVoltage,
		.CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength,
	};
	SPC_SetActiveModeCoreLDORegulatorConfig(SPC0, &ldo_cfg);

	/* Set the DCDC VDD regulator to 1.0 V voltage level */
	spc_active_mode_dcdc_option_t dcdc_cfg = {
		.DCDCVoltage	 = kSPC_DCDC_MidVoltage,
		.DCDCDriveStrength = kSPC_DCDC_NormalDriveStrength,
	};
	SPC_SetActiveModeDCDCRegulatorConfig(SPC0, &dcdc_cfg);

	/* Configure Flash wait-states to support 1V voltage level and 48MHz frequency */;
	FMU0->FCTRL = (FMU0->FCTRL & ~((uint32_t)FMU_FCTRL_RWSC_MASK)) | (FMU_FCTRL_RWSC(0x1U));

	/* Specifies the 1V operating voltage for the SRAM's read/write timing margin */
	spc_sram_voltage_config_t sram_cfg = {
		.operateVoltage	 = kSPC_sramOperateAt1P0V,
		.requestVoltageUpdate = true,
	};
	SPC_SetSRAMOperateVoltage(SPC0, &sram_cfg);

	/* Attach FROHF (48MHz) to MainClock, set clock divider to 1 */
	CLOCK_SetupFROHFClocking(48000000U);
	CLOCK_AttachClk(kFRO_HF_to_MAIN_CLK);
	CLOCK_SetClkDiv(kCLOCK_DivAhbClk, 1U);
}

static void mcxn_set_cpu_frequency_to_12mhz(void)
{
	/* Attach FRO12M to MainClock, set clock divider to 1 */
	CLOCK_AttachClk(kFRO12M_to_MAIN_CLK);
	CLOCK_SetClkDiv(kCLOCK_DivAhbClk, 1U);

	/* Set the LDO_CORE VDD regulator to 1.0 V voltage level */
	spc_active_mode_core_ldo_option_t ldo_cfg = {
		.CoreLDOVoltage = kSPC_CoreLDO_MidDriveVoltage,
		.CoreLDODriveStrength = kSPC_CoreLDO_NormalDriveStrength,
	};
	SPC_SetActiveModeCoreLDORegulatorConfig(SPC0, &ldo_cfg);

	/* Set the DCDC VDD regulator to 1.0 V voltage level */
	spc_active_mode_dcdc_option_t dcdc_cfg = {
		.DCDCVoltage = kSPC_DCDC_MidVoltage,
		.DCDCDriveStrength = kSPC_DCDC_NormalDriveStrength,
	};
	SPC_SetActiveModeDCDCRegulatorConfig(SPC0, &dcdc_cfg);

	/* Configure Flash wait-states to support 1V voltage level and 12MHz frequency */;
	FMU0->FCTRL = (FMU0->FCTRL & ~((uint32_t)FMU_FCTRL_RWSC_MASK)) | (FMU_FCTRL_RWSC(0x0U));

	/* Specifies the 1V operating voltage for the SRAM's read/write timing margin */
	spc_sram_voltage_config_t sram_cfg = {
		.operateVoltage	 = kSPC_sramOperateAt1P0V,
		.requestVoltageUpdate = true,
	};
	SPC_SetSRAMOperateVoltage(SPC0, &sram_cfg);
}
