#include <cmath>
#include "pico/platform.h"
#include "cvideo.h"
#include "menu.h"
#include "storage/pages/storage.h"

#define APRESS  0b0000'0000'0000'0001
#define BSAVE   0b0000'0000'0000'0010
#define BPRESS  0b0000'0000'0000'0100
#define DUPRESS 0b0000'0000'0000'1000
#define DDPRESS 0b0000'0000'0001'0000
#define DLPRESS 0b0000'0000'0010'0000
#define DRPRESS 0b0000'0000'0100'0000
#define LRPRESS 0b0000'0000'1000'0000
#define XPRESS  0b0000'0001'0000'0000
#define YPRESS  0b0000'0010'0000'0000
#define ZPRESS  0b0000'0100'0000'0000
#define SPRESS  0b0000'1000'0000'0000

void navigateMenu(unsigned char bitmap[],
		unsigned int &menu,
		int &itemIndex,
		uint8_t &redraw,
		bool &changeMade,
		const int currentCalStep,
		volatile uint8_t &pleaseCommit,
		uint16_t presses,
		const uint8_t increment,
		ControlConfig &controls);

void __time_critical_func(handleMenuButtons)(unsigned char bitmap[],
		unsigned int &menu,
		int &itemIndex,
		uint8_t &redraw,
		bool &changeMade,
		const int currentCalStep,
		volatile uint8_t &pleaseCommit,
		const Buttons &hardware,
		ControlConfig &controls) {
	uint16_t presses = 0;
	uint8_t changeIncrement = 1;

	//b button needs to accumulate before acting
	static uint8_t backAccumulator = 0;
	//a and dpad are locked out after acting
	const uint8_t buttonLockout = 10;// 1/6 of a second of ignoring button bounce
	const uint8_t dpadLockout = 15;// 1/4 of a second of ignoring button bounce
	static uint8_t aLockout = 0;
	static uint8_t zLockout = 0;
	static uint8_t sLockout = 0;
	static uint8_t lrLockout = 0;
	static uint8_t duLockout = 0;
	static uint8_t ddLockout = 0;
	static uint8_t dlLockout = 0;
	static uint8_t drLockout = 0;
	//only decrement the lockout if the button/direction is released so it doesn't repeat
	//it'll be unlocked after it hits zero
	if(!hardware.A && aLockout > 0) {
		aLockout--;
	}
	if(!hardware.L && !hardware.R && lrLockout > 0) {
		lrLockout--;
	}
	if(!hardware.Z && zLockout > 0) {
		zLockout--;
	}
	if(!hardware.S && sLockout > 0) {
		sLockout--;
	}
	//dpad directions do repeat
	duLockout = fmax(0, duLockout - 1);
	ddLockout = fmax(0, ddLockout - 1);
	dlLockout = fmax(0, dlLockout - 1);
	drLockout = fmax(0, drLockout - 1);

	static uint8_t duCounter = 0;//for controls that go faster when you hold them
	static uint8_t ddCounter = 0;//for controls that go faster when you hold them

	if(hardware.B) {
		backAccumulator++;
		if(backAccumulator == 5) {//only call save once per 0.5 second B hold
			presses = presses | BSAVE;
		}
		if(backAccumulator >= 30) {
			backAccumulator = 0;
			aLockout = 0;//make A available immediately after backing out
			presses = presses | BPRESS;
		}
	} else {
		if(backAccumulator > 0) {
			backAccumulator--;
		}
		if(hardware.A && aLockout == 0) {
			presses = presses | APRESS;
			aLockout = buttonLockout;
		} else if(hardware.Z && zLockout == 0) {
			presses = presses | ZPRESS;
			zLockout = buttonLockout;
		} else if(hardware.S && sLockout == 0) {
			presses = presses | SPRESS;
			sLockout = buttonLockout;
		} else if((hardware.L || hardware.R) && lrLockout == 0) {
			presses = presses | LRPRESS;
			lrLockout = buttonLockout;
		} else if(hardware.Dl && dlLockout == 0) {
			presses = presses | DLPRESS;
			dlLockout = dpadLockout;
			drLockout = 0;
			//also lock out perpendicular directions to prevent misinputs
			ddLockout = dpadLockout;
			duLockout = dpadLockout;
			//clear the repetition counters
			duCounter = 0;
			ddCounter = 0;
		} else if(hardware.Dr && drLockout == 0) {
			presses = presses | DRPRESS;
			drLockout = dpadLockout;
			dlLockout = 0;
			//also lock out perpendicular directions to prevent misinputs
			ddLockout = dpadLockout;
			duLockout = dpadLockout;
			//clear the repetition counters
			duCounter = 0;
			ddCounter = 0;
		} else if(hardware.Du && duLockout == 0) {
			presses = presses | DUPRESS;
			duLockout = dpadLockout;
			ddLockout = 0;
			//also lock out perpendicular directions to prevent misinputs
			dlLockout = dpadLockout;
			drLockout = dpadLockout;
			//Go by 10 after 10 steps
			if(duCounter <= 10) {
				duCounter++;
			}
			ddCounter = 0;
			changeIncrement = duCounter > 10 ? 10 : 1;
		} else if(hardware.Dd && ddLockout == 0) {
			presses = presses | DDPRESS;
			ddLockout = dpadLockout;
			duLockout = 0;
			//also lock out perpendicular directions to prevent misinputs
			dlLockout = dpadLockout;
			drLockout = dpadLockout;
			//Go by 10 after 10 steps
			if(ddCounter <= 10) {
				ddCounter++;
			}
			duCounter = 0;
			changeIncrement = ddCounter > 10 ? 10 : 1;
		} else {
			//clear the repetition counters
			if(duLockout == 0) {
				duCounter = 0;
			}
			if(ddLockout == 0) {
				ddCounter = 0;
			}
		}
		//Other presses that don't get locked out, not for navigation
		if(hardware.X) {
			presses = presses | XPRESS;
		}
		if(hardware.Y) {
			presses = presses | YPRESS;
		}
	}
	//handle actual navigation and settings changes
	if(presses) {
		navigateMenu(bitmap,
				menu,
				itemIndex,
				redraw,
				changeMade,
				currentCalStep,
				pleaseCommit,
				presses,
				changeIncrement,
				controls);
	}
	//handle always-redrawing screens since we don't always call navigation
	//redraw = 2 asks for a fast-path
	if(redraw == 0) {
		if(menu == MENU_STICKDBG && itemIndex == 0) {
			redraw = 2;
		} else if(menu == MENU_TRIGGER || menu == MENU_LTRIGGER || menu == MENU_RTRIGGER) {
			redraw = 2;
		} else if(menu == MENU_ASTICKCAL || menu == MENU_CSTICKCAL) {
			redraw = 2;
		} else if(menu == MENU_INPUTVIEW) {
			redraw = 2;
		}
	}
}

void navigateMenu(unsigned char bitmap[],
		unsigned int &menu,
		int &itemIndex,
		uint8_t &redraw,
		bool &changeMade,
		const int currentCalStep,
		volatile uint8_t &pleaseCommit,
		uint16_t presses,
		const uint8_t increment,
		ControlConfig &controls) {
	if(MenuIndex[menu][1] == 0) {
		if(presses & APRESS) {
			presses = 0;
			menu = MenuIndex[menu][2];
			itemIndex = 0;
			redraw = 1;
		}
	} else if(MenuIndex[menu][1] > 0) {
		//handle holding the B button as long as you're not on the splashscreen or calibrating
		if(presses & BPRESS) {
			if((menu == MENU_ASTICKCAL || menu == MENU_CSTICKCAL) && (currentCalStep >= 0)) {
				//do nothing
			} else {
				presses = 0;
				menu = MenuIndex[menu][0];
				itemIndex = 0;
				changeMade = false;
				redraw = 1;
			}
		}
		if(MenuIndex[menu][1] <= 6) {
			//if it's a submenu, handle a, dup, and ddown
			if(presses & APRESS) {
				presses = 0;
				menu = MenuIndex[menu][itemIndex + 2];
				itemIndex = 0;
				changeMade = false;
				redraw = 1;
				//don't return, we may want to handle setup things below
			} else if(presses & DUPRESS) {
				presses = 0;
				itemIndex = fmax(0, itemIndex-1);
				redraw = 1;
				return;
			} else if(presses & DDPRESS) {
				presses = 0;
				itemIndex = fmin(MenuIndex[menu][1]-1, itemIndex+1);
				redraw = 1;
			}
		}
		//Big switch case for controls for all the bottom level items
		static int tempInt1 = 0;
		static int tempInt2 = 0;
		switch(menu) {
			case MENU_ASTICKCAL:
				if(presses & (APRESS | LRPRESS)) {
					pleaseCommit = 4;
				} else if(presses & SPRESS) {
					pleaseCommit = 7;
				} else if(presses & ZPRESS) {
					pleaseCommit = 6;
				}
				return;
			case MENU_CSTICKCAL:
				if(presses & (APRESS | LRPRESS)) {
					pleaseCommit = 5;
				} else if(presses & SPRESS) {
					pleaseCommit = 7;
				} else if(presses & ZPRESS) {
					pleaseCommit = 6;
				}
				return;
			case MENU_AUTOINIT:
				if(!changeMade) {
					tempInt1 = controls.autoInit;
				}
				if(presses & DUPRESS) {
					controls.autoInit = true;
					changeMade = controls.autoInit != tempInt1;
					redraw = 1;
				} else if(presses & DDPRESS) {
					controls.autoInit = false;
					changeMade = controls.autoInit != tempInt1;
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setAutoInitSetting(controls.autoInit);
					tempInt1 = controls.autoInit;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_STICKDBG:
				//A cycles through pages
				if(presses & APRESS) {
					itemIndex++;
					if(itemIndex > 3) {
						itemIndex = 0;
					}
					redraw = 1;
				}
				return;
			case MENU_ASNAPBACK:
				if(!changeMade) {
					tempInt1 = controls.xSnapback;
					tempInt2 = controls.ySnapback;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.xSnapback = fmin(controls.snapbackMax, controls.xSnapback+1);
					} else {//itemIndex == 1
						controls.ySnapback = fmin(controls.snapbackMax, controls.ySnapback+1);
					}
					changeMade = (controls.xSnapback != tempInt1) || (controls.ySnapback != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.xSnapback = fmax(controls.snapbackMin, controls.xSnapback-1);
					} else {//itemIndex == 1
						controls.ySnapback = fmax(controls.snapbackMin, controls.ySnapback-1);
					}
					changeMade = (controls.xSnapback != tempInt1) || (controls.ySnapback != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setXSnapbackSetting(controls.xSnapback);
					setYSnapbackSetting(controls.ySnapback);
					tempInt1 = controls.xSnapback;
					tempInt2 = controls.ySnapback;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_AWAVE:
				if(!changeMade) {
					tempInt1 = controls.axWaveshaping;
					tempInt2 = controls.ayWaveshaping;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.axWaveshaping = fmin(controls.waveshapingMax, controls.axWaveshaping+1);
					} else {//itemIndex == 1
						controls.ayWaveshaping = fmin(controls.waveshapingMax, controls.ayWaveshaping+1);
					}
					changeMade = (controls.axWaveshaping != tempInt1) || (controls.ayWaveshaping != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.axWaveshaping = fmax(controls.waveshapingMin, controls.axWaveshaping-1);
					} else {//itemIndex == 1
						controls.ayWaveshaping = fmax(controls.waveshapingMin, controls.ayWaveshaping-1);
					}
					changeMade = (controls.axWaveshaping != tempInt1) || (controls.ayWaveshaping != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setWaveshapingSetting(controls.axWaveshaping, ASTICK, XAXIS);
					setWaveshapingSetting(controls.ayWaveshaping, ASTICK, YAXIS);
					tempInt1 = controls.axWaveshaping;
					tempInt2 = controls.ayWaveshaping;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_ASMOOTH:
				if(!changeMade) {
					tempInt1 = controls.axSmoothing;
					tempInt2 = controls.aySmoothing;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.axSmoothing = fmin(controls.smoothingMax, controls.axSmoothing+1);
					} else {//itemIndex == 1
						controls.aySmoothing = fmin(controls.smoothingMax, controls.aySmoothing+1);
					}
					changeMade = (controls.axSmoothing != tempInt1) || (controls.aySmoothing != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.axSmoothing = fmax(controls.smoothingMin, controls.axSmoothing-1);
					} else {//itemIndex == 1
						controls.aySmoothing = fmax(controls.smoothingMin, controls.aySmoothing-1);
					}
					changeMade = (controls.axSmoothing != tempInt1) || (controls.aySmoothing != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setXSmoothingSetting(controls.axSmoothing);
					setYSmoothingSetting(controls.aySmoothing);
					tempInt1 = controls.axSmoothing;
					tempInt2 = controls.aySmoothing;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_CSNAPBACK:
				if(!changeMade) {
					tempInt1 = controls.cxSmoothing;
					tempInt2 = controls.cySmoothing;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.cxSmoothing = fmin(controls.smoothingMax, controls.cxSmoothing+1);
					} else {//itemIndex == 1
						controls.cySmoothing = fmin(controls.smoothingMax, controls.cySmoothing+1);
					}
					changeMade = (controls.cxSmoothing != tempInt1) || (controls.cySmoothing != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.cxSmoothing = fmax(controls.smoothingMin, controls.cxSmoothing-1);
					} else {//itemIndex == 1
						controls.cySmoothing = fmax(controls.smoothingMin, controls.cySmoothing-1);
					}
					changeMade = (controls.cxSmoothing != tempInt1) || (controls.cySmoothing != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setCxSmoothingSetting(controls.cxSmoothing);
					setCySmoothingSetting(controls.cySmoothing);
					tempInt1 = controls.cxSmoothing;
					tempInt2 = controls.cySmoothing;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_CWAVE:
				if(!changeMade) {
					tempInt1 = controls.cxWaveshaping;
					tempInt2 = controls.cyWaveshaping;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.cxWaveshaping = fmin(controls.waveshapingMax, controls.cxWaveshaping+1);
					} else {//itemIndex == 1
						controls.cyWaveshaping = fmin(controls.waveshapingMax, controls.cyWaveshaping+1);
					}
					changeMade = (controls.cxWaveshaping != tempInt1) || (controls.cyWaveshaping != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.cxWaveshaping = fmax(controls.waveshapingMin, controls.cxWaveshaping-1);
					} else {//itemIndex == 1
						controls.cyWaveshaping = fmax(controls.waveshapingMin, controls.cyWaveshaping-1);
					}
					changeMade = (controls.cxWaveshaping != tempInt1) || (controls.cyWaveshaping != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setWaveshapingSetting(controls.cxWaveshaping, CSTICK, XAXIS);
					setWaveshapingSetting(controls.cyWaveshaping, CSTICK, YAXIS);
					tempInt1 = controls.cxWaveshaping;
					tempInt2 = controls.cyWaveshaping;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_COFFSET:
				if(!changeMade) {
					tempInt1 = controls.cXOffset;
					tempInt2 = controls.cYOffset;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.cXOffset = fmin(controls.cMax, controls.cXOffset+increment);
					} else {//itemIndex == 1
						controls.cYOffset = fmin(controls.cMax, controls.cYOffset+increment);
					}
					changeMade = (controls.cXOffset != tempInt1) || (controls.cYOffset != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.cXOffset = fmax(controls.cMin, controls.cXOffset-increment);
					} else {//itemIndex == 1
						controls.cYOffset = fmax(controls.cMin, controls.cYOffset-increment);
					}
					changeMade = (controls.cXOffset != tempInt1) || (controls.cYOffset != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setCxOffsetSetting(controls.cXOffset);
					setCyOffsetSetting(controls.cYOffset);
					tempInt1 = controls.cXOffset;
					tempInt2 = controls.cYOffset;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_REMAP:
				if(!changeMade) {
					tempInt1 = controls.jumpConfig;
				}
				if(presses & DUPRESS) {
					controls.jumpConfig = (JumpConfig) fmin(controls.jumpConfigMax, controls.jumpConfig+1);
					changeMade = controls.jumpConfig != tempInt1;
					redraw = 1;
				} else if(presses & DDPRESS) {
					controls.jumpConfig = (JumpConfig) fmax(controls.jumpConfigMin, controls.jumpConfig-1);
					changeMade = controls.jumpConfig != tempInt1;
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setJumpSetting(controls.jumpConfig);
					tempInt1 = controls.jumpConfig;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_RUMBLE:
				if(!changeMade) {
					tempInt1 = controls.rumble;
				}
				if(presses & DUPRESS) {
					controls.rumble = fmin(controls.rumbleMax, controls.rumble+1);
					changeMade = controls.rumble != tempInt1;
					redraw = 1;
				} else if(presses & DDPRESS) {
					controls.rumble = fmax(controls.rumbleMin, controls.rumble-1);
					changeMade = controls.rumble != tempInt1;
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setRumbleSetting(controls.rumble);
					tempInt1 = controls.rumble;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_LTRIGGER:
				if(!changeMade) {
					tempInt1 = controls.lConfig;
					tempInt2 = controls.lTriggerOffset;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.lConfig = fmin(controls.triggerConfigMax, controls.lConfig+1);
					} else {//itemIndex == 1
						controls.lTriggerOffset = fmin(controls.triggerMax, controls.lTriggerOffset+increment);
					}
					changeMade = (controls.lConfig != tempInt1) || (controls.lTriggerOffset != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.lConfig = fmax(controls.triggerConfigMin, controls.lConfig-1);
					} else {//itemIndex == 1
						controls.lTriggerOffset = fmax(controls.triggerMin, controls.lTriggerOffset-increment);
					}
					changeMade = (controls.lConfig != tempInt1) || (controls.lTriggerOffset != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setLSetting(controls.lConfig);
					setLOffsetSetting(controls.lTriggerOffset);
					tempInt1 = controls.lConfig;
					tempInt2 = controls.lTriggerOffset;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_RTRIGGER:
				if(!changeMade) {
					tempInt1 = controls.rConfig;
					tempInt2 = controls.rTriggerOffset;
				}
				if(presses & DLPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DRPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if(presses & DUPRESS) {
					if(itemIndex == 0) {
						controls.rConfig = fmin(controls.triggerConfigMax, controls.rConfig+1);
					} else {//itemIndex == 1
						controls.rTriggerOffset = fmin(controls.triggerMax, controls.rTriggerOffset+increment);
					}
					changeMade = (controls.rConfig != tempInt1) || (controls.rTriggerOffset != tempInt2);
					redraw = 1;
				} else if(presses & DDPRESS) {
					if(itemIndex == 0) {
						controls.rConfig = fmax(controls.triggerConfigMin, controls.rConfig-1);
					} else {//itemIndex == 1
						controls.rTriggerOffset = fmax(controls.triggerMin, controls.rTriggerOffset-increment);
					}
					changeMade = (controls.rConfig != tempInt1) || (controls.rTriggerOffset != tempInt2);
					redraw = 1;
				} else if((presses & BSAVE) && changeMade) {
					setRSetting(controls.rConfig);
					setROffsetSetting(controls.rTriggerOffset);
					tempInt1 = controls.rConfig;
					tempInt2 = controls.rTriggerOffset;
					changeMade = false;
					redraw = 1;
					pleaseCommit = 1;//ask the other thread to commit settings to flash
				}
				return;
			case MENU_RESET:
				if(presses & DUPRESS) {
					itemIndex = 0;
					redraw = 1;
				} else if(presses & DDPRESS) {
					itemIndex = 1;
					redraw = 1;
				} else if((presses & BSAVE) && itemIndex >= 2) {
					//if they briefly press B, back out
					itemIndex -= 2;
					redraw = 1;
				} else if((presses & APRESS) && itemIndex < 2) {
					//if they press A, go to the confirmation
					itemIndex += 2;
					redraw = 1;
				} else if((presses & APRESS) && itemIndex >= 2) {
					itemIndex -= 2;
					redraw = 1;
					if(itemIndex == 0) {
						//request a soft reset
						pleaseCommit = 2;
					} else {
						//request a hard reset
						pleaseCommit = 3;
					}
				}
			default:
				//do nothing
				return;
		}
	}
}

