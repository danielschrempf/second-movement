# Casio pet
Built for the sensor watch board swap for the Casio 593 module, it’s a virtual pet “watch face” for use with the “second-movement” open source repo.

## States
- 1 tic = Happy (smile)
- 2 tic =  Confused (eyebrows)
- 3 tic = Upset (s mouth)
- 4 tic = Angry (downturn mouth)
- 6 tic = Dead (tombstone)
- Resurrect - long hold alarm (ghost with blink)

## Buffs / Debuffs
- Play (accelerometer taps) - Adds -0.5 tic
- Hug (long hold light) - Adds up to -1.0 tic 0.25 tic at a time
- Feed (button light) - 1 tic every missed day
- Clean poo (button alarm) - generates 0.25 tic after every feed, after that, adds +1 tic every 1 tic unless "swept" with button. Max 1 poo, animation plays on revisit if one has been made. Sweep clears all.
- Sleeps from 9pm to 5am - If disturbed from sleep, 0.25 tic each time.

## Controls
- Mode
	- Reserved for system. Used for switching to different faces.
- Light -
	- Short - Feed
	- Long - Hug
- Alarm On-Off/24hr -
	- Short - Sweep Poo
	- Long - Resurrect
- Accelerometer
	- Shake - Play

## Animations
- [x] Happy
- [x] Confused
- [x] Upset
- [x] Angry
- [x] Dead

- [x] Resurrect
- [x] Poo
- [x] Play small
- [x] Play big
- [x] Barf
- [x] Eat
- [x] Kiss
- [x] Snore
- [x] Wake
## Sounds
- [ ] Snore
- [ ] Kiss
- [ ] Barf
- [ ] Eat

## Logic

- When switched to clock face,
	- if tic >= 6
		- hold dead static
			- until resurrect
				- then play ressurect
				- return tic to 0
				- play "resurrect" animation
				- blink
	- if "day"
		- If "morning" 5am to 10am
			- play wake animation
			- play state animation
			- if poo, play poo animation
			- blink
		- if "afternoon" 10am to 9pm
			- if poo, load static poo graphic
			- play state animation
			- blink
		- Main loop
			- play state animation
			- blink
				- if interaction
					- play animation based on interaction
					- increment tic
					- blink
				- if poo
					- play poo animation
					- blink
	- If "night" 9pm to 5am
		- if poo, load static poo graphic
		- play sleep animation
		- if play, hug, or feed
			- play wake animation
			- blink
			- Loop 
				- play animation based on current tic
				- blink
			- Until play, hug, or feed
				- ignore buff for play, hug or feed
				- add 0.25 to tic
				- return to loop
		- if sweep
			- clear poo state
	- If Mode hit
		- return to rest
		- accumulate 1 tic every 6hrs

Feed() (could be recursive)
- Button logs food up to 4
- While food > 0
- if 3 seconds after last feed press
	- eat one pip
	- Start separate poo countdown (2 tic)
	- Subtract 0.25 tic
	- Wait 1 second
		- If no button pushed, eat
		- Else start from 3 seconds

Play()
- if motion detected over threshold
- For nausea > 3
- play play small animation
- Add buff
- start 5 second countdown
	- If counter = 0
		- Return done
	- if counter > 0 &&  motion
	- inc nausea 1
	- Loop