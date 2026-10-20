// Identical clocks, route and feature state for native and browser captures.
module.exports = [
  { name: 'day', time: 510, weather: 1790900000, travel: 162000 },
  {
    name: 'day-540p', time: 510, weather: 1790900000, travel: 162000,
    options: '&height=540', height: 540,
  },
  {
    name: 'night', time: 437625, weather: 1790900000, travel: 0,
    options: '&surface=0',
  },
  {
    name: 'night-no-stars', time: 437625, weather: 1790900000, travel: 0,
    options: '&surface=0&stars=0',
  },
  { name: 'storm', time: 510, weather: 1260, travel: 162000 },
  { name: 'night-clouds', time: 1200, weather: 1790900000, travel: 162000 },
  {
    name: 'thin-clouds', time: 510, weather: 1790900000, travel: 162000,
    options: '&volumetric=0',
  },
];
