'use strict';

const $ = id => document.getElementById(id);
const DEFAULT_ENTITIES = Object.freeze({
  ac:'climate.may_lanh_sharp',bedroomAc:'climate.ken_airconditioner',cob:'light.light_cob_1', cover:'cover.khach_rem',
  toshiba:'fan.toshiba', ceilingFan:'fan.khach_khach_cong_tac_2_left',
  eating:'light.khach_khach_cong_tac_2_right', sofa:'light.khach_khach_cong_tac_3_right',
  ceiling:'light.khach_khach_cong_tac_3_center', lobby:'light.khach_smart_light_switch',
  balcony:'light.ban_cong_den_ban_cong_l1', washerFinish:'binary_sensor.washer_washer_status_finish',
  rain:'binary_sensor.cam_bien_ro_nuoc_moes', door:'binary_sensor.khach_cua_chinh_contact',
  window:'binary_sensor.living_room_door_window_sensor_32ea_opening',
  balconyDoor:'binary_sensor.khach_cua_bancon_contact', temperature:'sensor.khach_temperature_temperature',
  humidity:'sensor.khach_temperature_humidity', pm25:'sensor.air_quality_pms5003_pm2_5',co2:'',voc:'',bedroomPm25:'',bedroomVoc:'',
  air:'sensor.air_quality_pms5003_air_quality', weather:'weather.khach_weather_home_2',
  power:'sensor.khach_dual_pzem_meter_pzem_1_power', energy:'sensor.ken_energy_monthly',
  cost:'sensor.ken_electricity_cost', bedroomCo2:'sensor.sol_air_quality_mhz19b_carbondioxide',
  bedroomTemperature:'sensor.ken_nhiet_do_temperature', bedroomHumidity:'sensor.ken_nhiet_do_humidity',
  bedroomBathLight:'light.ken_ken_cong_tac_top', bedroomLight:'light.ken_ken_cong_tac_bottom',
  bedroomFan:'fan.ken_quat_tran', hallway:'light.khach_khach_cong_tac_sanh',
  kitchenLight:'light.bep_den_bep', hood:'light.bep_hut_mui',
  waterFlow:'sensor.kitchen_water_flow_luu_luong', waterToday:'sensor.kitchen_water_flow_su_dung_hom_nay',
  waterMonth:'sensor.kitchen_water_flow_su_dung_thang_nay', waterTotal:'sensor.water_flow_tong_luong_nuoc',
  washerActive:'switch.giat_may_giat_homekit_may_giat', washerRemaining:'sensor.washer_remaining_time',
  wcLight:'light.ve_sinh_chung_ve_sinh_left', waterHeater:'switch.ve_sinh_cong_tac_right',
  dryer:'binary_sensor.contactor', washerDaily:'sensor.washer_status_daily_energy',
  dryerDaily:'sensor.living_room_smart_plug_bl0937_daily_energy', totalEnergy:'sensor.tasmota_energy_total_2',
  piTemp:'sensor.system_monitor_processor_temperature', piDisk:'sensor.system_monitor_disk_free',
  piRam:'sensor.system_monitor_memory_usage', inductionStatus:'sensor.beptu_status',
  inductionPower:'sensor.khach_dual_pzem_meter_pzem_2_power',
  inductionDaily:'sensor.dual_pzem_meter_pzem_2_daily_energy',
  inductionMonthly:'sensor.dual_pzem_meter_pzem_2_monthly_energy',
  inductionTotal:'sensor.dual_pzem_meter_pzem_2_total_energy', washerPower:'sensor.washer_status_power',
  washerMonthly:'',washerTotal:'',dryerPower:'sensor.washer_status_power',dryerMonthly:'',dryerTotal:'',music:'media_player.phicomm_r1_fd73', aiBox:'sensor.ken_aibox'
});

const ENTITY_LABELS = {
  ac:'Máy lạnh Sharp',bedroomAc:'Máy lạnh phòng ngủ',cob:'Đèn COB',cover:'Rèm phòng khách',toshiba:'Quạt Toshiba',ceilingFan:'Quạt trần phòng khách',
  eating:'Đèn bàn ăn',sofa:'Đèn sofa',ceiling:'Đèn trần',lobby:'Đèn tủ giầy',balcony:'Đèn ban công',
  washerFinish:'Máy giặt xong',rain:'Cảm biến mưa/rò nước',door:'Cửa chính',window:'Cửa sổ Ken',balconyDoor:'Cửa ban công',
  temperature:'Nhiệt độ phòng khách',humidity:'Độ ẩm phòng khách',pm25:'PM2.5',air:'Chất lượng không khí',weather:'Thời tiết',
  power:'Công suất tổng',energy:'Điện tháng',cost:'Tiền điện',bedroomCo2:'CO₂ phòng ngủ',bedroomTemperature:'Nhiệt độ phòng ngủ',
  bedroomHumidity:'Độ ẩm phòng ngủ',bedroomBathLight:'Đèn WC phòng ngủ',bedroomLight:'Đèn ngủ',bedroomFan:'Quạt phòng ngủ',
  hallway:'Đèn sảnh',kitchenLight:'Đèn bếp',hood:'Hút mùi',waterFlow:'Lưu lượng nước',waterToday:'Nước hôm nay',
  waterMonth:'Nước tháng',waterTotal:'Tổng nước',washerActive:'Trạng thái máy giặt',washerRemaining:'Thời gian còn lại',
  wcLight:'Đèn WC chung',waterHeater:'Nước nóng',dryer:'Máy sấy',washerDaily:'Điện máy giặt hôm nay',dryerDaily:'Điện máy sấy hôm nay',
  totalEnergy:'Tổng điện năng',piTemp:'Máy chủ Homeassistant · Nhiệt độ CPU',piDisk:'Máy chủ Homeassistant · Dung lượng trống',piRam:'Máy chủ Homeassistant · RAM',inductionStatus:'Trạng thái bếp từ',
  inductionPower:'Công suất bếp từ',inductionDaily:'Điện bếp từ hôm nay',inductionMonthly:'Điện bếp từ tháng',inductionTotal:'Tổng điện bếp từ',
  washerPower:'Công suất máy giặt',washerMonthly:'Máy giặt tháng này',washerTotal:'Tổng điện máy giặt',dryerPower:'Công suất máy sấy',dryerMonthly:'Máy sấy tháng này',dryerTotal:'Tổng điện máy sấy',music:'Loa Phicomm R1',aiBox:'AI BOX'
};

const ROOMS = ['Tổng quan','P. khách','P. ngủ','Bếp','P. giặt'];
const NAV = [
  {key:'home',label:'Nhà',icon:'home'}, {key:'music',label:'Nhạc',icon:'music'},
  {key:'security',label:'An ninh',icon:'shield'}, {key:'lights',label:'Đèn',icon:'light'},
  {key:'climate',label:'Khí hậu',icon:'climate'}, {key:'energy',label:'Điện',icon:'energy'},
  {key:'settings',label:'Cài đặt',icon:'settings'}
];

const DEVICE = {
  eating:{entity:'eating',name:'Bàn ăn',icon:'light',kind:'toggle'}, sofa:{entity:'sofa',name:'Sofa',icon:'light',kind:'toggle'},
  ceiling:{entity:'ceiling',name:'Đèn trần',icon:'light',kind:'toggle'}, lobby:{entity:'lobby',name:'Tủ giầy',icon:'light',kind:'toggle'},
  balcony:{entity:'balcony',name:'Ban công',icon:'light',kind:'toggle'}, ceilingFan:{entity:'ceilingFan',name:'Quạt trần',icon:'fan',kind:'toggle'},
  bedroomBathLight:{entity:'bedroomBathLight',name:'Đèn WC',icon:'light',kind:'toggle'}, bedroomLight:{entity:'bedroomLight',name:'Đèn ngủ',icon:'light',kind:'toggle'},
  bedroomFan:{entity:'bedroomFan',name:'Quạt trần',icon:'fan',kind:'toggle'}, hallway:{entity:'hallway',name:'Sảnh',icon:'light',kind:'toggle'},
  wcLight:{entity:'wcLight',name:'Đèn WC',icon:'light',kind:'toggle'}, waterHeater:{entity:'waterHeater',name:'Nước nóng',icon:'water-heater',kind:'toggle'},
  kitchenLight:{entity:'kitchenLight',name:'Đèn bếp',icon:'light',kind:'toggle'}, hood:{entity:'hood',name:'Hút mùi',icon:'fan',kind:'toggle'},
  ac:{entity:'ac',name:'Máy lạnh',icon:'snow',kind:'ac'},bedroomAc:{entity:'bedroomAc',name:'Máy lạnh',icon:'snow',kind:'ac'},toshiba:{entity:'toshiba',name:'Toshiba',icon:'fan',kind:'fan'},
  cover:{entity:'cover',name:'Rèm',icon:'cover',kind:'cover'}, cob:{entity:'cob',name:'Đèn COB',icon:'light',kind:'cob'},
  rain:{entity:'rain',name:'Mưa',icon:'rain',kind:'binary'}, door:{entity:'door',name:'Cửa chính',icon:'door',kind:'binary'},
  window:{entity:'window',name:'Cửa sổ Ken',icon:'window',kind:'binary'}, balconyDoor:{entity:'balconyDoor',name:'Cửa ban công',icon:'door',kind:'binary'},
  dryer:{entity:'dryer',powerEntity:'dryerPower',monthEntity:'dryerMonthly',totalEntity:'dryerTotal',name:'Máy sấy',icon:'dryer',kind:'dryer'}, washer:{entity:'washerActive',remainingEntity:'washerRemaining',finishEntity:'washerFinish',powerEntity:'washerPower',monthEntity:'washerMonthly',totalEntity:'washerTotal',name:'Máy giặt',icon:'washer',kind:'washer'},
  water:{entity:'waterFlow',dailyEntity:'waterToday',monthEntity:'waterMonth',totalEntity:'waterTotal',name:'Lọc nước',icon:'purifier',kind:'water'}, induction:{entity:'inductionStatus',powerEntity:'inductionPower',monthEntity:'inductionMonthly',totalEntity:'inductionTotal',name:'Bếp từ',icon:'induction',kind:'induction'}
};
const BASE_DEVICE_KEYS=new Set(Object.keys(DEVICE));
const BASE_ENTITY_LABELS={...ENTITY_LABELS};

const ICON_PATHS={
  home:'M3 11.5 12 4l9 7.5V21h-6v-6H9v6H3z',music:'M9 18V6l10-2v12M9 18a3 3 0 1 1-3-3 3 3 0 0 1 3 3Zm10-2a3 3 0 1 1-3-3 3 3 0 0 1 3 3Z',shield:'M12 3 20 6v5c0 5-3.4 8.5-8 10-4.6-1.5-8-5-8-10V6z',light:'M9 18h6m-5 3h4m-6-8a6 6 0 1 1 8 0c-1.2 1-1.7 2-1.8 3H9.8C9.7 15 9.2 14 8 13Z',climate:'M8 15a4 4 0 1 0 8 0c0-1.5-.8-2.8-2-3.5V5a2 2 0 0 0-4 0v6.5A4 4 0 0 0 8 15Z',energy:'m13 2-8 12h6l-1 8 8-12h-6z',settings:'M4 6h16M8 3v6M4 12h16m-4-3v6M4 18h16M10 15v6',power:'M12 2v10M6.3 5.3a8 8 0 1 0 11.4 0',fan:'M12 12c-5-1-7-4-5-6 2-2 5 1 5 6 1-5 4-7 6-5 2 2-1 5-6 5 5 1 7 4 5 6-2 2-5-1-5-6-1 5-4 7-6 5-2-2 1-5 6-5Z',snow:'M12 2v20M4 7l16 10M20 7 4 17M8 4l4 4 4-4M8 20l4-4 4 4',cover:'M5 3h14v18H5zM8 3v18m8-18v18',rain:'M7 14 12 4l5 10a5 5 0 1 1-10 0Zm2 1a3 3 0 0 0 3 3',door:'M5 3h13v18H5zm9 9h.01',window:'M4 4h16v16H4zm8 0v16M4 12h16',washer:'M4 3h16v18H4zm4 2h2m3 0h3m-9 9a5 5 0 1 0 10 0 5 5 0 0 0-10 0Z',dryer:'M4 3h16v18H4zm4 2h2m3 0h3m-5 6c3-2 5 1 3 3s0 4 2 4',water:'M12 3 6 13a6 6 0 1 0 12 0z',purifier:'M7 3h10a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2Zm2 4h6m-4 4h2m-3 4h4m-6 6v1m8-1v1',induction:'M4 4h16v16H4zm4 4a4 4 0 1 0 8 0 4 4 0 0 0-8 0Zm1 8h6','water-heater':'M7 3h10a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2Zm2 4h6m-5 10h4M8 21v1m8-1v1','weather-sun':'M12 3v2m0 14v2M3 12h2m14 0h2M5.6 5.6 7 7m10 10 1.4 1.4m0-12.8L17 7M7 17l-1.4 1.4M8 12a4 4 0 1 0 8 0 4 4 0 0 0-8 0Z','weather-partly':'M5 16h13a3 3 0 0 0 0-6 6 6 0 0 0-11-2 4 4 0 0 0-2 8Zm1-10 1-1m5-2v2m-5 6H4','weather-cloud':'M5 18h13a4 4 0 0 0 .2-8 6 6 0 0 0-11.4 1A3.5 3.5 0 0 0 5 18Z','weather-moon':'M19 15a8 8 0 0 1-10-10 8 8 0 1 0 10 10Z','weather-rain':'M5 15h13a4 4 0 0 0 .2-8 6 6 0 0 0-11.4 1A3.5 3.5 0 0 0 5 15Zm3 3-1 3m6-3-1 3m6-3-1 3','weather-storm':'M5 14h13a4 4 0 0 0 .2-8 6 6 0 0 0-11.4 1A3.5 3.5 0 0 0 5 14Zm7 2-2 3h3l-2 3','weather-fog':'M4 8h16M6 12h12M4 16h16','weather-wind':'M3 8h11c3 0 3-4 0-4m-11 8h16c3 0 3-4 0-4M3 16h10c3 0 3 4 0 4'
};
ICON_PATHS.cover='M4 4h16M6 5c0 6 1 10 5 15H5m13-15c0 6-1 10-5 15h6M12 5v15';
ICON_PATHS.purifier='M3 6h5V4h8v2h2a2 2 0 0 1 2 2v10h2v3h-5V9h-2v8a5 5 0 0 1-10 0V9H3zM8 9v8a2 2 0 0 0 4 0V9';
ICON_PATHS.leak='M12 3 7 12a6 6 0 1 0 10 0zM4 20h16M7 17h10';
ICON_PATHS.dishwasher='M5 3h14v18H5zM8 6h.01M11 6h5M8 14a4 4 0 1 0 8 0 4 4 0 0 0-8 0zM10 14h4';
ICON_PATHS.downlight='M6 5h12l-2 6H8zM9 14l-2 4m5-4v5m3-5 2 4';
ICON_PATHS.chandelier='M12 3v5M7 8h10M8 8c0 4-2 5-4 5m12-5c0 4 2 5 4 5M7 13h2v3H7zm8 0h2v3h-2zm-4-2h2v4h-2z';
ICON_PATHS['ceiling-light']='M5 5h14M7 7h10l2 5a7 7 0 0 1-14 0z';
ICON_PATHS['wall-light']='M5 4v16M5 8h7m0-3v10m0-8h5l2 3-2 3h-5';
ICON_PATHS['night-light']='M8 4h8l2 8H6zM12 12v7M8 20h8';
ICON_PATHS['led-strip']='M4 6h16v12H4zM8 6v12m4-12v12m4-12v12';
ICON_PATHS['ceiling-fan']='M12 3v5m0 4c-4-1-7-3-6-6 2-2 5 0 6 6 1-4 3-7 6-6 2 2 0 5-6 6 4 1 7 3 6 6-2 2-5 0-6-6-1 4-3 7-6 6-2-2 0-5 6-6z';
ICON_PATHS['wall-fan']='M4 4h16v16H4zM12 8v4m0 0c-3-3-5 1-2 2m2-2c3-3 5 1 2 2m-2-2c-3 3 1 5 2 2';
ICON_PATHS['exhaust-fan']='M4 4h16v16H4zM12 12c-4-1-5-4-3-6 2 0 3 2 3 6 1-4 4-5 6-3 0 2-2 3-6 3 4 1 5 4 3 6-2 0-3-2-3-6-1 4-4 5-6 3 0-2 2-3 6-3z';
ICON_PATHS.switch='M4 7h16M4 17h16M9 4v6m6 4v6';
ICON_PATHS.server='M5 3h14v7H5zM5 14h14v7H5zM8 6.5h.01m3 0h5M8 17.5h.01m3 0h5';
ICON_PATHS.disk='M5 5h14l2 11v4H3v-4zM3 16h18M16 18h.01m3 0h.01';
ICON_PATHS.memory='M5 6h14v12H5zM8 9h3v6H8zm6 0h2v6h-2zM8 3v3m4-3v3m4-3v3M8 18v3m4-3v3m4-3v3M2 9h3m-3 6h3m14-6h3m-3 6h3';
const SENSOR_ICON_CHOICES=[['door','Cửa đi'],['window','Cửa sổ'],['rain','Mưa'],['leak','Rò nước'],['purifier','Máy lọc nước'],['washer','Máy giặt'],['dryer','Máy sấy'],['dishwasher','Máy rửa chén']];
const LIGHT_ICON_CHOICES=[['light','Bóng đèn'],['downlight','Đèn âm trần'],['switch','Công tắc'],['ceiling-light','Đèn ốp trần'],['wall-light','Đèn gắn tường'],['night-light','Đèn ngủ'],['led-strip','LED dây'],['ceiling-fan','Quạt trần'],['wall-fan','Quạt treo tường'],['exhaust-fan','Quạt hút']];
function iconSvg(name){const path=ICON_PATHS[name]||ICON_PATHS.settings;return `<svg class="ui-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="${path}"/></svg>`}
function applianceIconSvg(kind){
  if(kind==='washer')return '<svg class="ui-icon appliance-motion-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 3h16v18H4zM8 5h2m3 0h3"/><g class="washer-drum"><circle cx="12" cy="14" r="5"/><path d="M9 13c1.4-1.8 4.7-1.4 5.8.7M10 17c1.6.8 3.8.2 4.7-1.2"/></g></svg>';
  if(kind==='dryer')return '<svg class="ui-icon appliance-motion-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 3h16v18H4zM8 5h2m3 0h3"/><g class="dryer-drum"><circle cx="12" cy="14" r="5"/><path d="M9 13c2-2 5 0 4 2s1 3 3 2"/></g></svg>';
  if(kind==='induction')return '<svg class="ui-icon appliance-motion-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 4h16v16H4zM8 10a4 4 0 1 0 8 0 4 4 0 0 0-8 0Z"/><g class="induction-heat"><path d="M9 18c-1-1 1-2 0-3m3 3c-1-1 1-2 0-3m3 3c-1-1 1-2 0-3"/></g></svg>';
  if(kind==='water')return '<svg class="ui-icon appliance-motion-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M3 6h5V4h8v2h2a2 2 0 0 1 2 2v10h2v3h-5V9h-2v8a5 5 0 0 1-10 0V9H3zM8 9v8a2 2 0 0 0 4 0V9"/><g class="water-drop"><path d="M18 11c-2 2.5-2 3.5 0 4.5 2-1 2-2 0-4.5Z"/></g></svg>';
  return iconSvg(kind);
}
const MEDIA_PATHS={shuffle:'M4 7h3c5 0 5 10 10 10h3m-3-3 3 3-3 3M4 17h3c2 0 3-1 4-3m2-5c1-1 2-2 4-2h3m-3-3 3 3-3 3',previous:'M7 5v14m12-14L9 12l10 7z',play:'m8 5 11 7-11 7z',pause:'M9 5v14m6-14v14',next:'M17 5v14M5 5l10 7-10 7z',repeat:'M4 7h13l-2-2m2 2-2 2M20 17H7l2 2m-2-2 2-2',volume:'M4 10v4h4l5 4V6L8 10zm12-1a4 4 0 0 1 0 6m2-9a8 8 0 0 1 0 12',mute:'M4 10v4h4l5 4V6L8 10zm12 0 5 5m0-5-5 5'};
function mediaIcon(name){return `<svg class="ui-icon media-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="${MEDIA_PATHS[name]}"/></svg>`}

const METRIC_TYPES=Object.freeze({
  temperature:{label:'Nhiệt độ',color:'gold',digits:1,unit:'°C'},
  humidity:{label:'Độ ẩm',color:'blue',digits:0,unit:'%'},
  pm25:{label:'PM2.5',color:'green',digits:0,unit:'µg/m³'},
  air:{label:'Không khí',color:'blue'},
  co2:{label:'CO₂',color:'blue',digits:0,unit:'ppm'},
  voc:{label:'VOC',color:'green',digits:1,unit:''}
});
const DEFAULT_METRIC_TYPES=['temperature','humidity','pm25','air'];
ICON_PATHS.voc='M12 9a3 3 0 1 0 0 6 3 3 0 0 0 0-6ZM5 4a2 2 0 1 0 0 4 2 2 0 0 0 0-4ZM19 3a2 2 0 1 0 0 4 2 2 0 0 0 0-4ZM19 17a2 2 0 1 0 0 4 2 2 0 0 0 0-4ZM4 16a2 2 0 1 0 0 4 2 2 0 0 0 0-4ZM7 7l3 3m4-1 3-3m-3 8 3 4m-7-4-4 3';
ICON_PATHS['arrow-up']='m6 15 6-6 6 6';
ICON_PATHS['arrow-down']='m6 9 6 6 6-6';
ICON_PATHS.pencil='m5 16-1 4 4-1L19 8l-3-3zM14 7l3 3';
ICON_PATHS.close='M6 6l12 12M18 6 6 18';

const POS = (key,x,y,size) => ({key,x,y,size});
const LAYOUTS = {
  home: [
    [POS('ac',0,0,'large'),POS('toshiba',192,0,'large'),POS('cover',384,0,'wide'),POS('cob',384,96,'wide'),POS('washer',672,0,'medium'),POS('dryer',672,192,'compact'),POS('water',480,192,'large'),POS('eating',0,192,'compact'),POS('sofa',96,192,'compact'),POS('ceiling',192,192,'compact'),POS('lobby',288,192,'compact'),POS('balcony',0,288,'compact'),POS('ceilingFan',96,288,'compact'),POS('waterHeater',192,288,'compact'),POS('hallway',288,288,'compact'),POS('induction',672,288,'compact')],
    [POS('ac',0,0,'large'),POS('toshiba',192,0,'large'),POS('cover',384,0,'wide'),POS('cob',384,96,'wide'),POS('balcony',0,192,'compact'),POS('ceilingFan',96,192,'compact'),POS('wcLight',192,192,'compact'),POS('waterHeater',288,192,'compact'),POS('rain',384,192,'medium'),POS('eating',0,288,'compact'),POS('sofa',96,288,'compact'),POS('ceiling',192,288,'compact'),POS('hallway',288,288,'compact'),POS('lobby',384,288,'compact')],
    [POS('window',0,0,'medium'),POS('bedroomBathLight',192,0,'compact'),POS('bedroomLight',288,0,'compact'),POS('bedroomFan',384,0,'compact'),POS('bedroomAc',480,0,'large')],
    [POS('kitchenLight',0,0,'compact'),POS('hood',96,0,'compact'),POS('water',192,0,'large'),POS('induction',384,0,'compact')],
    [POS('washer',0,0,'medium'),POS('dryer',192,0,'compact')]
  ],
  security: [
    [POS('washer',0,0,'medium'),POS('dryer',192,0,'compact'),POS('door',384,0,'medium'),POS('window',576,0,'medium'),POS('balconyDoor',192,96,'medium'),POS('rain',384,96,'medium')],
    [POS('rain',0,0,'medium')],[POS('window',0,0,'medium')],[],[POS('washer',0,0,'medium'),POS('dryer',192,0,'compact')]
  ],
  lights: [
    [POS('cob',0,0,'wide'),POS('eating',288,0,'compact'),POS('sofa',384,0,'compact'),POS('ceiling',480,0,'compact'),POS('lobby',576,0,'compact'),POS('hallway',672,0,'compact'),POS('balcony',768,0,'compact'),POS('wcLight',0,96,'compact'),POS('bedroomBathLight',96,96,'compact'),POS('bedroomLight',192,96,'compact'),POS('kitchenLight',288,96,'compact')],
    [POS('cob',0,0,'wide'),POS('eating',288,0,'compact'),POS('sofa',384,0,'compact'),POS('ceiling',480,0,'compact'),POS('lobby',576,0,'compact'),POS('hallway',672,0,'compact'),POS('balcony',768,0,'compact'),POS('wcLight',0,96,'compact')],
    [POS('bedroomBathLight',0,0,'compact'),POS('bedroomLight',96,0,'compact')],[POS('kitchenLight',0,0,'compact')],[]
  ],
  climate: [
    [POS('ac',0,0,'large'),POS('toshiba',192,0,'large'),POS('cover',384,0,'wide'),POS('ceilingFan',672,0,'compact'),POS('waterHeater',768,0,'compact'),POS('bedroomFan',384,96,'compact'),POS('hood',480,96,'compact')],
    [POS('ac',0,0,'large'),POS('toshiba',192,0,'large'),POS('cover',384,0,'wide'),POS('ceilingFan',672,0,'compact'),POS('waterHeater',768,0,'compact')],
    [POS('bedroomFan',0,0,'compact')],[POS('hood',0,0,'compact')],[]
  ]
};

let config = loadConfig();
let states = {};
let connected = false;
let demo = false;
let currentNav = 'home';
let currentRoom = 0;
let settingsEntityMode = false;
let metricEditorRoomId = null;
let rangeDragging = false;
let activeLightPopup = null;
let layoutEditMode = false;
let layoutEdits = loadLayoutEdits();
let rooms=layoutEdits.rooms||ROOMS.map((name,index)=>({id:String(index),name,source:index}));
Object.assign(DEVICE,layoutEdits.customDevices||{});for(const [key,def] of Object.entries(layoutEdits.customDevices||{})){const name=layoutEdits.names[key]||def.name;ENTITY_LABELS[key]=`${name} · Trạng thái`;for(const [prop,label] of [['remainingEntity','Thời gian còn lại'],['finishEntity','Máy giặt xong'],['powerEntity','Công suất hiện tại'],['dailyEntity','Hôm nay'],['monthEntity','Tháng này'],['totalEntity','Tổng tiêu thụ']])if(def[prop])ENTITY_LABELS[def[prop]]=`${name} · ${label}`}
for(const [key,icon] of Object.entries(layoutEdits.icons||{}))if(DEVICE[key])DEVICE[key].icon=icon;
const optimisticControls={cover:null,cob:null};
let entityHoldCard=null,entityHoldCancel=null;
let layoutHoldCancel=null,openingHoldPointer=null,suppressHoldClick=false;
function cancelLayoutLongPress(){layoutHoldCancel?.()}
function cancelEntityLongPress(){entityHoldCancel?.()}
function modalOpen(){return $('detail-dialog').open||$('config-dialog').open}
function clearTextSelection(){document.getSelection?.()?.removeAllRanges()}
function markHeldPopupGesture(pointerId){
  openingHoldPointer=pointerId;suppressHoldClick=true;
  cancelLayoutLongPress();cancelEntityLongPress();clearTextSelection();
}
function installGestureGuards(){
  addEventListener('pointerdown',e=>{if(e.isPrimary!==false&&openingHoldPointer===null)suppressHoldClick=false},{capture:true});
  const finish=e=>{
    cancelLayoutLongPress();
    if(openingHoldPointer===null||e.pointerId!==openingHoldPointer)return;
    openingHoldPointer=null;e.preventDefault();e.stopImmediatePropagation();
    const dialog=$('detail-dialog');
    if(dialog.open){clearTextSelection();dialog.focus({preventScroll:true});requestAnimationFrame(()=>{if(dialog.open)dialog.focus({preventScroll:true})})}
  };
  for(const event of ['pointerup','pointercancel'])addEventListener(event,finish,{capture:true});
  addEventListener('click',e=>{
    if(!suppressHoldClick||e.detail===0)return;
    suppressHoldClick=false;e.preventDefault();e.stopImmediatePropagation();
  },{capture:true});
  addEventListener('blur',()=>{cancelLayoutLongPress();cancelEntityLongPress();openingHoldPointer=null;pointerHeld=false});
  addEventListener('pointerdown',e=>{if(e.isPrimary!==false)pointerHeld=true},{capture:true,passive:true});
  // click runs before this timeout, so the tap always lands on the element that was pressed
  for(const event of ['pointerup','pointercancel'])addEventListener(event,e=>{if(e.isPrimary===false)return;pointerHeld=false;if(liveRenderSkipped)setTimeout(()=>{if(liveRenderSkipped)liveRender()},0)},{capture:true,passive:true});
}
let toastTimer;
let idleTimer;
let displaySettings = loadDisplaySettings();
let history = loadHistory();
let musicSource = localStorage.getItem('ken-music-source') || 'ma';
if(!config.usePhicomm)musicSource='ma';
let directSpeakerStatus = 'offline';
let directSpeakerState = {playing:false,title:'AI BOX',artist:'Phicomm R1',volume:null,position:0,duration:0,results:[],playlists:[],libraryMode:'search'};
let haBrowse={category:'artists',items:[],root:null,loading:false,loaded:false};
let musicRenderTimer;
function editingField(){return document.activeElement?.matches?.('input,select,textarea')}
let pointerHeld=false,liveRenderSkipped=false;
function suppressLiveRender(){return pointerHeld||!!layoutHoldCancel||!!entityHoldCard||rangeDragging||layoutEditMode||editingField()||(currentNav==='settings'&&settingsEntityMode)}
function liveRender(){if(suppressLiveRender())liveRenderSkipped=true;else{liveRenderSkipped=false;render()}}
function queueMusicRender(){if(currentNav!=='music'||musicSource!=='aibox'||editingField())return;clearTimeout(musicRenderTimer);musicRenderTimer=setTimeout(()=>renderMusic(),120)}
const directSpeaker = new DirectSpeaker(next=>{directSpeakerState={...next};queueMusicRender()},status=>{directSpeakerStatus=status;queueMusicRender()});

let statesFlushTimer=null,entityListDirty=true;
function flushStates(){
  statesFlushTimer=null;
  rememberLightStates();
  syncDashboardLightCards();
  syncLightPopup();
  updateHistory();
  entityListDirty=true;
  if(!$('entity-list').options.length&&!(currentNav==='settings'&&settingsEntityMode)&&!editingField())fillEntityList();
  liveRender();
}
const client = new HAClient(next => {
  states = next;
  if(!statesFlushTimer)statesFlushTimer=setTimeout(flushStates,120);
}, status => {
  connected = status === 'ready';
  if(status === 'auth_invalid') toast('Token Home Assistant không hợp lệ');
  renderStatus(status);
  liveRender();
});

function loadConfig(){
  let saved = {};
  try{saved = JSON.parse(localStorage.getItem('ken-config-v3') || '{}')}catch{}
  if(!saved.url){
    try{
      const old = JSON.parse(localStorage.getItem('ken-config') || '{}');
      const map = old.map || {};
      saved.url = old.url || '';
      const legacyEntities={weather:map.weather,pm25:map.pm,power:map.power,energy:map.energy,cost:map.cost,cob:map.light1,cover:map.cover,ac:map.climate};
      saved.entities={...Object.fromEntries(Object.entries(legacyEntities).filter(([,value])=>typeof value==='string'&&value.trim())),...(saved.entities||{})};
    }catch{}
  }
  return {url:saved.url || '',speakerAddress:saved.speakerAddress || 'http://192.168.1.236:8081/',usePhicomm:saved.usePhicomm!==false,theme:saved.theme||'default',rgbPresets:saved.rgbPresets||{},lightColorTypes:saved.lightColorTypes||{},lastLightColors:saved.lastLightColors||{},lastLightBrightness:saved.lastLightBrightness||{},entities:{...DEFAULT_ENTITIES,...(saved.entities || {})}};
}
function loadDisplaySettings(){try{return {...{brightness:100,timeout:-1},...JSON.parse(localStorage.getItem('ken-display-v3') || '{}')}}catch{return {brightness:100,timeout:-1}}}
function loadHistory(){try{return {...{days:{},months:{},bills:{},lastTotal:null,lastDate:null,lastMonth:null},...JSON.parse(localStorage.getItem('ken-history-v3') || '{}')}}catch{return {days:{},months:{},bills:{},lastTotal:null,lastDate:null,lastMonth:null}}}
function loadLayoutEdits(){try{return {...{positions:{},names:{},hidden:{},added:{},customDevices:{},icons:{},removedDevices:[],rooms:null,metricRows:{}},...JSON.parse(localStorage.getItem('ken-layout-v1')||'{}')}}catch{return {positions:{},names:{},hidden:{},added:{},customDevices:{},icons:{},removedDevices:[],rooms:null,metricRows:{}}}}
function layoutKey(){return `${currentNav}:${rooms[currentRoom]?.id||currentRoom}`}
let sharedVersion=0,sharedSaveTimer=null,sharedStarted=false,sharedSaving=false,sharedSavePending=false;
function sharedPayload(){return {config:{url:config.url||'',speakerAddress:config.speakerAddress||'',usePhicomm:config.usePhicomm!==false,theme:config.theme||'default',rgbPresets:config.rgbPresets||{},lightColorTypes:config.lightColorTypes||{},lastLightColors:config.lastLightColors||{},lastLightBrightness:config.lastLightBrightness||{},entities:{...config.entities}},layoutEdits:{...layoutEdits,rooms}}}
function restoreSharedData(data){if(!data)return;config={...config,...(data.config||{}),theme:data.config?.theme||'default',rgbPresets:data.config?.rgbPresets||{},lightColorTypes:data.config?.lightColorTypes||{},lastLightColors:data.config?.lastLightColors||{},lastLightBrightness:{...(data.config?.lastLightBrightness||{}),...(config.lastLightBrightness||{})},entities:{...DEFAULT_ENTITIES,...(data.config?.entities||{})}};layoutEdits={...loadLayoutEdits(),...(data.layoutEdits||{}),metricRows:data.layoutEdits?.metricRows||{}};rooms=layoutEdits.rooms||ROOMS.map((name,index)=>({id:String(index),name,source:index}));for(const key of Object.keys(DEVICE))if(!BASE_DEVICE_KEYS.has(key))delete DEVICE[key];Object.assign(DEVICE,layoutEdits.customDevices||{});for(const key of Object.keys(ENTITY_LABELS))delete ENTITY_LABELS[key];Object.assign(ENTITY_LABELS,BASE_ENTITY_LABELS);for(const [key,def] of Object.entries(layoutEdits.customDevices||{})){const name=layoutEdits.names[key]||def.name;ENTITY_LABELS[key]=`${name} · Trạng thái`;for(const [prop,label] of [['remainingEntity','Thời gian còn lại'],['finishEntity','Máy giặt xong'],['powerEntity','Công suất hiện tại'],['dailyEntity','Hôm nay'],['monthEntity','Tháng này'],['totalEntity','Tổng tiêu thụ']])if(def[prop])ENTITY_LABELS[def[prop]]=`${name} · ${label}`}for(const [key,icon] of Object.entries(layoutEdits.icons||{}))if(DEVICE[key])DEVICE[key].icon=icon;localStorage.setItem('ken-config-v3',JSON.stringify(config));localStorage.setItem('ken-layout-v1',JSON.stringify(layoutEdits));applyTheme()}
async function loadSharedDashboard(){try{const response=await fetch('/api/config',{cache:'no-store'});if(!response.ok)throw Error(`HTTP ${response.status}`);const remote=await response.json();sharedVersion=Number(remote.version)||0;if(remote.data)restoreSharedData(remote.data);else await saveSharedDashboard(true);sharedStarted=true;return true}catch(error){console.warn('Shared dashboard unavailable:',error);sharedStarted=false;return false}}
async function saveSharedDashboard(immediate=false){clearTimeout(sharedSaveTimer);if(!immediate){sharedSaveTimer=setTimeout(()=>saveSharedDashboard(true),250);return}if(sharedSaving){sharedSavePending=true;return}sharedSaving=true;sharedSavePending=false;try{const response=await fetch('/api/config',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({version:sharedVersion,data:sharedPayload()})});if(response.status===409){sharedSavePending=false;const remote=await response.json();sharedVersion=Number(remote.version)||sharedVersion;if(remote.data)restoreSharedData(remote.data);toast('Cấu hình đã thay đổi trên máy khác, Dashboard vừa đồng bộ lại');if(sharedStarted){buildNavigation();buildEntityForm();render()}return}if(!response.ok)throw Error(`HTTP ${response.status}`);const result=await response.json();sharedVersion=Number(result.version)||sharedVersion;sharedStarted=true}catch(error){console.warn('Cannot save shared dashboard:',error)}finally{sharedSaving=false;if(sharedSavePending){sharedSavePending=false;saveSharedDashboard()}}}
function persistConfig(){localStorage.setItem('ken-config-v3',JSON.stringify(config));entityListDirty=true;saveSharedDashboard()}
function saveLayoutEdits(){layoutEdits.rooms=rooms;localStorage.setItem('ken-layout-v1',JSON.stringify(layoutEdits));entityListDirty=true;saveSharedDashboard()}
async function pollSharedDashboard(){if(metricEditorRoomId!==null||layoutHoldCancel||entityHoldCard||document.hidden||editingField()||layoutEditMode||sharedSaving)return;try{const response=await fetch('/api/config',{cache:'no-store'});if(!response.ok)return;const remote=await response.json(),version=Number(remote.version)||0;if(version>sharedVersion&&remote.data){sharedVersion=version;restoreSharedData(remote.data);buildNavigation();buildEntityForm();render();toast('Đã đồng bộ cấu hình Dashboard')}}catch{}}
function removedDeviceKeys(){return new Set([...(layoutEdits.removedDevices||[]),...Object.values(layoutEdits.hidden||{}).flat()])}
function baseRoomLayout(nav=currentNav,index=currentRoom){const source=rooms[index]?.source;return source==null?[]:(LAYOUTS[nav]?.[source]||[])}
function sourceRoomIndexes(){return rooms[currentRoom]?.source===0?rooms.map((_,index)=>index):[currentRoom]}
function roomSourceDevices(index){const room=rooms[index],source=room?.source,items=[];if(source!=null){const page=`home:${room.id}`,hidden=new Set(layoutEdits.hidden[page]||[]);for(const pos of LAYOUTS.home?.[source]||[])if(!hidden.has(pos.key))items.push({key:pos.key,size:pos.size})}for(const [page,added] of Object.entries(layoutEdits.added||{})){const roomId=page.slice(page.indexOf(':')+1);if(roomId!==String(room?.id))continue;const hidden=new Set(layoutEdits.hidden[page]||[]);for(const [key,pos] of Object.entries(added||{}))if(!hidden.has(key))items.push({key,size:pos.size})}return items}
function categoryMatch(def,nav){const lightIcons=new Set(['light','downlight','chandelier','ceiling-light','wall-light','night-light','led-strip']),fanIcons=new Set(['fan','ceiling-fan','wall-fan','exhaust-fan']);if(nav==='security')return ['binary','sensor','washer','dryer','water','induction'].includes(def.kind);if(nav==='lights')return def.kind==='cob'||(def.kind==='toggle'&&lightIcons.has(def.icon));if(nav==='climate')return ['ac','fan','cover'].includes(def.kind)||(def.kind==='toggle'&&fanIcons.has(def.icon));return false}
function packCategoryDevices(items){const placed=[];for(const item of items){const def=DEVICE[item.key];if(!def)continue;const size=cardSizeFor(def,item.size||deviceSize(item.key)),[w,h]=sizePixels(size);let spot=null;for(let y=0;y<=376-h&&!spot;y+=96)for(let x=0;x<=856-w;x+=96){if(placed.every(pos=>{const [pw,ph]=sizePixels(pos.size);return !(x<pos.x+pw&&x+w>pos.x&&y<pos.y+ph&&y+h>pos.y)})){spot={key:item.key,x,y,size};break}}if(spot)placed.push(spot)}return placed}
function aggregateDeviceLayout(){const seen=new Set(),items=[],removed=removedDeviceKeys();for(const index of sourceRoomIndexes())for(const item of roomSourceDevices(index)){const def=DEVICE[item.key];if(!def||removed.has(item.key)||!categoryMatch(def,currentNav))continue;const entity=config.entities[def.entity]||def.entity;if(seen.has(entity))continue;seen.add(entity);items.push(item)}const packed=packCategoryDevices(items),saved=layoutEdits.positions[layoutKey()]||{};return packed.map(pos=>({...pos,...(saved[pos.key]||{})}))}
function currentDeviceLayout(){if(['security','lights','climate'].includes(currentNav))return aggregateDeviceLayout();const key=layoutKey(),removed=removedDeviceKeys(),saved=layoutEdits.positions[key]||{},base=baseRoomLayout().filter(pos=>!removed.has(pos.key)).map(pos=>({...pos,...(saved[pos.key]||{})})),added=Object.entries(layoutEdits.added[key]||{}).filter(([deviceKey])=>!removed.has(deviceKey)).map(([deviceKey,pos])=>({key:deviceKey,...pos,...(saved[deviceKey]||{})}));return [...base,...added]}

function state(key){const exact=states[config.entities[key]];if(exact)return exact;if(key==='washerDaily')return Object.values(states).find(s=>{const name=String(s.attributes?.friendly_name||'').toLocaleLowerCase('vi');return /(đo điện ngày|điện.*hôm nay|daily energy)/i.test(name)&&/(máy giặt|washer)/i.test(name)});return undefined}
function usable(key){const s=state(key);return !!s && !['unknown','unavailable','none','None'].includes(s.state)}
function numeric(key){if(!usable(key))return null;const n=Number(state(key).state);return Number.isFinite(n)?n:null}
function unit(key){return state(key)?.attributes?.unit_of_measurement || ''}
function formatNumber(value,digits=1){return value == null || !Number.isFinite(value) ? '-' : new Intl.NumberFormat('vi-VN',{maximumFractionDigits:digits}).format(value)}
function formatSensor(key,digits=1,fallbackUnit=''){const n=numeric(key);return n==null?'-':`${formatNumber(n,digits)} ${unit(key)||fallbackUnit}`.trim()}
function formatRemaining(key){if(!usable(key))return '-';const sensor=state(key),raw=String(sensor.state).trim(),looksLikeCompletion=/^\d{4}-\d{2}-\d{2}[T\s]/.test(raw)||sensor.attributes?.device_class==='timestamp';if(looksLikeCompletion){const completion=Date.parse(raw);if(Number.isFinite(completion)){const minutes=Math.max(0,Math.ceil((completion-Date.now())/60000));return `${Math.floor(minutes/60)}:${String(minutes%60).padStart(2,'0')}`}}const parts=raw.split(':').map(Number);if(parts.length>=2&&parts.every(Number.isFinite)){const hours=parts.length===3?parts[0]:Math.floor(parts[0]),minutes=parts[1];return `${hours}:${String(minutes).padStart(2,'0')}`}const value=Number(raw);if(Number.isFinite(value)){const u=unit(key),minutes=/hour/i.test(u)?Math.round(value*60):/^s(ec|econd)?/i.test(u)?Math.round(value/60):Math.round(value);return `${Math.floor(minutes/60)}:${String(minutes%60).padStart(2,'0')}`}return raw}
function isOn(key){const v=state(key)?.state;return ['on','open','opening','playing','heat','cool','dry','fan_only','auto'].includes(v)}
function canControl(key){return (demo || connected) && usable(key)}

function validLightBrightness(value){
  if(value==null||value==='')return null;
  const raw=Number(value);
  return Number.isFinite(raw)&&raw>0&&raw<=255?Math.round(raw):null;
}
function lightBrightness(key){
  const raw=validLightBrightness(state(key)?.attributes?.brightness);
  const remembered=validLightBrightness(config.lastLightBrightness?.[config.entities[key]]);
  return isOn(key)?raw??remembered:remembered??raw;
}
function lightDisplayPercent(key,raw=lightBrightness(key)){
  return state(key)?.state==='off'||raw==null?0:Math.round(raw*100/255);
}
function rememberLightStates(){
  if(demo)return;
  let changed=false;
  config.lastLightBrightness=config.lastLightBrightness||{};
  config.lastLightColors=config.lastLightColors||{};
  for(const [key,entity] of Object.entries(config.entities)){
    if(!entity?.startsWith('light.'))continue;
    const s=state(key),raw=validLightBrightness(s?.attributes?.brightness);
    const pending=config.entities[activeLightPopup?.key]===entity?activeLightPopup?.pendingBrightness:null;
    const awaitingTarget=pending&&pending.until>Date.now()&&(pending.raw===0?s?.state!=='off':raw!=null&&Math.abs(raw-pending.raw)>2);
    if(raw&&!awaitingTarget&&(s?.state==='on'||(s?.state==='off'&&!validLightBrightness(config.lastLightBrightness[entity])))&&config.lastLightBrightness[entity]!==raw){
      config.lastLightBrightness[entity]=raw;changed=true;
    }
    if(s?.state==='on'&&!awaitingTarget&&(s.attributes?.rgb_color||s.attributes?.hs_color||s.attributes?.color_temp_kelvin||s.attributes?.color_temp)){
      const color=cobCurrentColor(s);
      if(config.lastLightColors[key]!==color){config.lastLightColors[key]=color;changed=true}
    }
  }
  if(changed)localStorage.setItem('ken-config-v3',JSON.stringify(config));
}
function lightDisplayColor(key){const saved=config.lastLightColors?.[key];return !isOn(key)&&/^#[0-9a-f]{6}$/i.test(saved||'')?saved:cobCurrentColor(state(key))}
function lightSnapshot(key){
  const s=state(key),mode=s?.attributes?.color_mode;
  const kelvin=Number(s?.attributes?.color_temp_kelvin)||(Number(s?.attributes?.color_temp)?Math.round(1000000/Number(s.attributes.color_temp)):null);
  return {on:isOn(key),brightness:lightColorData(key,{}).brightness??lightBrightness(key),rgb:mode==='color_temp'||(!s?.attributes?.rgb_color&&!s?.attributes?.hs_color&&kelvin)?null:hexToRgb(lightDisplayColor(key)),kelvin};
}
function lightColorData(key,data){
  const pending=activeLightPopup?.key===key?activeLightPopup.pendingBrightness:null;
  const raw=pending?.raw>0&&pending.until>Date.now()?pending.raw:lightBrightness(key);
  // Missing off-state brightness must never be converted into a forced 100%.
  return raw?{...data,brightness:raw}:{...data};
}
function sendPopupColor(key,colorData){
  const data=lightColorData(key,colorData);
  if(data.brightness&&activeLightPopup?.key===key)activeLightPopup.pendingBrightness={raw:data.brightness,until:Date.now()+3000};
  return call(key,'turn_on',data);
}
function setPopupBrightness(key,value,colorData={}){
  const pct=Math.max(0,Math.min(100,Math.round(Number(value))));
  if(activeLightPopup?.key===key)activeLightPopup.pendingBrightness={raw:Math.round(pct*255/100),until:Date.now()+3000};
  return pct===0?call(key,'turn_off'):call(key,'turn_on',{...colorData,brightness_pct:pct});
}
function resetLightPopup(){activeLightPopup?.cleanup?.();activeLightPopup=null}
function syncLightPopup(){
  const popup=activeLightPopup;
  if(!popup||!$('detail-dialog').open||!popup.slider.isConnected)return;
  const raw=lightBrightness(popup.key),pending=popup.pendingBrightness,off=state(popup.key)?.state==='off';
  const settled=!pending||pending.until<=Date.now()||(pending.raw===0?!isOn(popup.key):isOn(popup.key)&&raw!=null&&Math.abs(raw-pending.raw)<=2);
  if(!popup.interacting&&(off||settled)){
    if(settled)popup.pendingBrightness=null;
    const pct=lightDisplayPercent(popup.key,raw);
    popup.slider.value=String(pct);popup.label.textContent=pct+'%';
  }
  if(popup.currentColor)popup.currentColor.style.background=lightDisplayColor(popup.key);
}
function bindPopupBrightness(key,slider,label,colorData=()=>({})){
  const popup={key,slider,label,interacting:false,pendingBrightness:null};
  activeLightPopup=popup;
  slider.onpointerdown=e=>{popup.interacting=true;slider.setPointerCapture?.(e.pointerId)};
  slider.oninput=()=>{popup.interacting=true;label.textContent=slider.value+'%'};
  slider.onchange=()=>{popup.interacting=false;setPopupBrightness(key,slider.value,colorData())};
  slider.onpointerup=slider.onpointercancel=slider.onblur=()=>{popup.interacting=false;setTimeout(syncLightPopup,0)};
  return popup;
}
function urlOrigin(url){try{return new URL(url).origin}catch{return ''}}
// The HA address is shared through /api/config, the token is not: never send a token to an address it was not entered for.
function savedToken(url=config.url){
  for(const store of [sessionStorage,localStorage]){
    const token=store.getItem('ken-token');if(!token)continue;
    let bound=store.getItem('ken-token-origin');
    if(!bound&&(bound=urlOrigin(url)))store.setItem('ken-token-origin',bound);
    return bound&&bound===urlOrigin(url)?token:'';
  }
  return '';
}
function hasStoredToken(){return !!(sessionStorage.getItem('ken-token')||localStorage.getItem('ken-token'))}
function escapeHtml(value){return String(value??'').replace(/[&<>'"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;',"'":'&#39;','"':'&quot;'}[c]))}
function toast(message){$('toast').textContent=message;$('toast').style.display='block';clearTimeout(toastTimer);toastTimer=setTimeout(()=>$('toast').style.display='none',3800)}

function buildNavigation(){
  $('room-tabs').style.gridTemplateColumns=`repeat(${Math.max(1,rooms.length)},1fr)`;
  $('room-tabs').replaceChildren(...rooms.map((room,index)=>{
    const b=document.createElement('button');b.type='button';b.className='room-button';b.textContent=room.name;let timer,managed=false;b.oncontextmenu=e=>e.preventDefault();b.onpointerdown=e=>{managed=false;timer=setTimeout(()=>{managed=true;markHeldPopupGesture(e.pointerId);showRoomManager()},1800)};for(const event of ['pointerup','pointercancel','pointerleave'])b.addEventListener(event,()=>clearTimeout(timer));b.onclick=()=>{if(managed)return;layoutEditMode=false;currentRoom=index;render()};return b;
  }));
  $('side-nav').replaceChildren(...NAV.map(item=>{
    const b=document.createElement('button');b.type='button';b.className='nav-button';b.dataset.nav=item.key;b.innerHTML=`<span class="nav-icon">${iconSvg(item.icon)}</span><span class="nav-label">${item.label}</span>`;
    b.onclick=()=>{layoutEditMode=false;currentNav=item.key;if(item.key==='music'&&config.usePhicomm&&musicSource==='aibox'&&config.speakerAddress&&directSpeakerStatus==='offline'){try{directSpeaker.connect(config.speakerAddress)}catch(error){toast(error.message)}}render();resetIdle()};return b;
  }));
}

function render(){
  rememberLightStates();
  syncLightPopup();
  renderTop();
  const full=['music','energy','settings'].includes(currentNav);
  $('room-tabs').style.display=full?'none':'grid';
  $('metric-row').style.display=full?'none':'grid';
  $('content').style.top=full?'112px':'208px';
  $('content').style.height=full?'480px':'376px';
  [...$('side-nav').children].forEach(b=>b.classList.toggle('selected',b.dataset.nav===currentNav));
  [...$('room-tabs').children].forEach((b,i)=>{b.classList.toggle('selected',i===currentRoom);b.disabled=full});
  if(['home','security','lights','climate'].includes(currentNav)){renderMetrics();renderDevices()}
  else if(currentNav==='music')renderMusic();
  else if(currentNav==='energy')renderEnergy();
  else renderSettings();
}

function renderTop(){
  const weather=state('weather');
  const conditions={sunny:['weather-sun','Nắng'],partlycloudy:['weather-partly','Ít mây'],cloudy:['weather-cloud','Nhiều mây'],'clear-night':['weather-moon','Trời quang'],rainy:['weather-rain','Mưa'],pouring:['weather-rain','Mưa lớn'],lightning:['energy','Dông'],'lightning-rainy':['weather-storm','Dông'],fog:['weather-fog','Sương mù'],snowy:['snow','Tuyết'],windy:['weather-wind','Có gió'],'windy-variant':['weather-wind','Có gió']};
  const [icon,label]=conditions[weather?.state]||['weather-cloud',usable('weather')?weather.state:'--'];
  $('weather-icon').innerHTML=iconSvg(icon);
  const wt=Number(weather?.attributes?.temperature);$('weather-temp').textContent=Number.isFinite(wt)?`${formatNumber(wt,1)}°`:'--';$('weather-text').textContent=label;
  $('power-value').textContent=usable('power')?formatSensor('power',0,'W'):'--';
  $('energy-value').textContent=usable('energy')?formatSensor('energy',1,'kWh'):'--';
  const c=numeric('cost');$('cost-value').textContent=c==null?'--':Math.abs(c)>=1e6?`${formatNumber(c/1e6,2)}M VND`:Math.abs(c)>=1e3?`${formatNumber(c/1e3,0)}k VND`:`${formatNumber(c,0)} VND`;
}

function metricEntityKeys(roomId=rooms[currentRoom]?.id){
  const room=rooms.find(item=>String(item.id)===String(roomId)),source=room?.source;
  if(source===0||source===1)return {temperature:'temperature',humidity:'humidity',pm25:'pm25',air:'air',co2:'co2',voc:'voc'};
  if(source===2)return {temperature:'bedroomTemperature',humidity:'bedroomHumidity',pm25:'bedroomPm25',air:'bedroomAir',co2:'bedroomCo2',voc:'bedroomVoc'};
  const prefix=`room_${room?.id||'default'}_`;
  return Object.fromEntries(Object.keys(METRIC_TYPES).map(type=>[type,prefix+type]));
}
function normalizeMetricRow(row){
  if(!Array.isArray(row))return DEFAULT_METRIC_TYPES.map(type=>({type,name:METRIC_TYPES[type].label}));
  const seen=new Set();
  return row.filter(item=>item&&Object.hasOwn(METRIC_TYPES,item.type)&&!seen.has(item.type)&&seen.add(item.type))
    .map(item=>({type:item.type,name:String(item.name||METRIC_TYPES[item.type].label).trim().slice(0,80)||METRIC_TYPES[item.type].label}));
}
function metricRow(roomId=rooms[currentRoom]?.id){return normalizeMetricRow(layoutEdits.metricRows?.[String(roomId)])}
function entityCatalog(){
  const metricKeys=new Set(rooms.flatMap(room=>Object.values(metricEntityKeys(room.id))));
  const labels=new Map(Object.entries(ENTITY_LABELS).filter(([key])=>!metricKeys.has(key))),seen=new Set();
  for(const room of rooms)for(const item of metricRow(room.id)){
    const key=metricEntityKeys(room.id)[item.type];if(seen.has(key))continue;
    seen.add(key);labels.set(key,`${room.name} · ${item.name}`);
  }
  return [...labels];
}
function saveMetricRow(roomId,row){
  layoutEdits.metricRows={...(layoutEdits.metricRows||{}),[String(roomId)]:normalizeMetricRow(row)};
  saveLayoutEdits();buildEntityForm();renderMetrics();
}
function metricIcon(type){
  if(type==='temperature')return '<svg class="ui-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M9 15a4 4 0 1 0 6 0c0-1.2-.5-2.2-1.5-3V5a1.5 1.5 0 0 0-3 0v7c-1 .8-1.5 1.8-1.5 3Zm3-1v3"/></svg>';
  if(type==='humidity')return '<svg class="ui-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 3 6.5 12.5a6.3 6.3 0 1 0 11 0zM9 15a3 3 0 0 0 3 3"/></svg>';
  if(type==='pm25')return '<svg class="ui-icon pm25-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="9"/><circle cx="9" cy="9" r="1.2"/><circle cx="15" cy="8" r=".9"/><circle cx="15.5" cy="14.5" r="1.5"/><circle cx="8.5" cy="15" r=".7"/><path d="M6 12h5m2-1h5"/></svg>';
  if(type==='co2')return '<span class="co2-symbol">CO₂</span>';
  return iconSvg(type==='air'?'weather-wind':type);
}
function metricValue(type,key){
  if(type!=='air'){const def=METRIC_TYPES[type];return formatSensor(key,def.digits,def.unit)}
  const value=usable(key)?state(key).state:'-';
  const map={excellent:'Rất tốt',great:'Rất tốt',good:'Tốt',moderate:'Vừa',fair:'Vừa',poor:'Kém',unhealthy:'Xấu',hazardous:'Nguy hại'};
  return map[String(value).toLowerCase()]||value;
}
function installTopEntityEditors(){
  const targets=[['weather-temp',()=>['weather','Thời tiết']],['power-value',()=>['power','Điện']],['energy-value',()=>['energy','Tháng này']],['cost-value',()=>['cost','Tiền điện']]];
  targets.forEach(([target,resolve])=>{
    const node=$(target),card=node?.closest('article')||node;if(!card)return;
    bindEntityHold(card,()=>resolve()[1],()=>showTopEntityEditor(...resolve()),3000);
  });
}
function bindEntityHold(card,title,open,delay=3000){
  let timer=null,x=0,y=0;
  const cancel=()=>{clearTimeout(timer);timer=null;if(entityHoldCard===card)entityHoldCard=null;if(entityHoldCancel===cancel)entityHoldCancel=null;card.classList.remove('entity-holding')};
  card.tabIndex=0;card.setAttribute('role','button');card.setAttribute('aria-label',`Đổi entity ${title()}`);
  card.onpointerdown=e=>{
    if(e.isPrimary===false||e.button>0||modalOpen())return;
    e.preventDefault();e.stopPropagation();cancelLayoutLongPress();clearTextSelection();
    cancelEntityLongPress();cancel();x=e.clientX;y=e.clientY;entityHoldCard=card;entityHoldCancel=cancel;card.classList.add('entity-holding');
    card.setPointerCapture?.(e.pointerId);
    timer=setTimeout(()=>{const present=card.isConnected&&!modalOpen();cancel();if(present){markHeldPopupGesture(e.pointerId);open()}},delay);
  };
  card.onpointermove=e=>{if(timer&&Math.hypot(e.clientX-x,e.clientY-y)>8)cancel()};
  for(const event of ['pointerup','pointercancel','pointerleave','lostpointercapture'])card.addEventListener(event,cancel);
  card.oncontextmenu=e=>e.preventDefault();
  card.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();cancel();open()}};
}
function showTopEntityEditor(key,title){showCardEntityEditor([[key,'Entity Home Assistant']],title)}
function showServerEntityEditor(){showCardEntityEditor([['piTemp','Nhiệt độ CPU'],['piDisk','Dung lượng trống'],['piRam','RAM']],'Máy chủ Homeassistant')}
function showCardEntityEditor(fields,title){
  $('detail-close').hidden=true;
  const pencil='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m5 16-1 4 4-1L19 8l-3-3zM14 7l3 3"/></svg>';
  $('detail-content').innerHTML=`<div class="top-entity-title"><span>${pencil}</span><h2>${escapeHtml(title)}</h2></div><form id="top-entity-form" class="new-device-form">${fields.map(([key,label])=>`<label>${escapeHtml(label)}<input id="${fields.length===1?'top-entity-input':'top-entity-'+key}" data-card-entity="${key}" list="entity-list" autocomplete="off" placeholder="Chọn hoặc nhập entity" value="${escapeHtml(config.entities[key]||'')}"></label>`).join('')}<div><button id="top-entity-cancel" type="button">Hủy</button><button type="submit">Lưu entity</button></div></form>`;
  $('top-entity-cancel').onclick=()=>$('detail-dialog').close();
  $('top-entity-form').onsubmit=e=>{
    e.preventDefault();
    for(const input of $('top-entity-form').querySelectorAll('[data-card-entity]')){const key=input.dataset.cardEntity;config.entities[key]=input.value.trim()||DEFAULT_ENTITIES[key]||''}
    persistConfig();buildEntityForm();$('detail-dialog').close();render();toast(`Đã lưu entity ${title}`);
  };
  $('detail-dialog').showModal();focusDialogWithoutInput();
}

function renderMetrics(){
  const root=$('metric-row'),row=metricRow(),keys=metricEntityKeys();
  root.style.setProperty('--metric-count',Math.max(1,row.length));
  root.classList.toggle('metric-row-dense',row.length>=5);root.replaceChildren();
  if(!row.length){
    const card=document.createElement('article');card.className='panel metric metric-empty';card.textContent='+ Cảm biến';
    bindEntityHold(card,()=> 'Thêm cảm biến',()=>showMetricManager(),3000);root.append(card);return;
  }
  for(const item of row){
    const def=METRIC_TYPES[item.type],card=document.createElement('article');card.className='panel metric';card.dataset.metricType=item.type;
    card.innerHTML=`<span ${item.type==='pm25'?'id="metric-third-icon"':''} class="metric-icon ${def.color}">${metricIcon(item.type)}</span><span ${item.type==='pm25'?'id="metric-third-label"':''} class="metric-label">${escapeHtml(item.name)}</span><strong id="metric-${item.type==='pm25'?'third':item.type}">${escapeHtml(metricValue(item.type,keys[item.type]))}</strong>`;
    bindEntityHold(card,()=>item.name,()=>showMetricManager(item.type),3000);
    card.setAttribute('aria-label',`Sắp xếp cảm biến: ${item.name}`);root.append(card);
  }
}
function showMetricManager(selectedType=null,roomId=rooms[currentRoom]?.id){
  const room=rooms.find(item=>String(item.id)===String(roomId));if(!room)return;
  metricEditorRoomId=String(roomId);$('detail-close').hidden=true;
  const row=metricRow(roomId),keys=metricEntityKeys(roomId);
  $('detail-content').innerHTML=`<h2>Sắp xếp cảm biến</h2><p class="metric-manager-room">${escapeHtml(room.name)}</p><div class="room-manager metric-manager">${row.map((item,index)=>`<div data-metric-item="${item.type}" class="${item.type===selectedType?'selected':''}"><span class="metric-icon ${METRIC_TYPES[item.type].color}">${metricIcon(item.type)}</span><div class="metric-manager-name"><b>${escapeHtml(item.name)}</b><small>${escapeHtml(config.entities[keys[item.type]]||'Chưa chọn entity')}</small></div><button type="button" data-metric-up="${index}" aria-label="Chuyển lên" ${index===0?'disabled':''}>${iconSvg('arrow-up')}</button><button type="button" data-metric-down="${index}" aria-label="Chuyển xuống" ${index===row.length-1?'disabled':''}>${iconSvg('arrow-down')}</button><button type="button" data-metric-edit="${item.type}" aria-label="Sửa cảm biến">${iconSvg('pencil')}</button><button type="button" data-metric-remove="${item.type}" aria-label="Xóa cảm biến">${iconSvg('close')}</button></div>`).join('')}</div><div class="room-manager-actions metric-manager-actions"><button id="metric-add" type="button" ${row.length===Object.keys(METRIC_TYPES).length?'disabled':''}>+ Cảm biến</button><button id="metric-close" type="button">Đóng</button><button id="metric-done" type="button">Xong</button></div>`;
  const apply=(next,selected)=>{saveMetricRow(roomId,next);showMetricManager(selected,roomId)};
  $('detail-content').querySelectorAll('[data-metric-up]').forEach(button=>button.onclick=()=>{const i=Number(button.dataset.metricUp);if(i>0){const next=[...row];[next[i-1],next[i]]=[next[i],next[i-1]];apply(next,row[i].type)}});
  $('detail-content').querySelectorAll('[data-metric-down]').forEach(button=>button.onclick=()=>{const i=Number(button.dataset.metricDown);if(i<row.length-1){const next=[...row];[next[i],next[i+1]]=[next[i+1],next[i]];apply(next,row[i].type)}});
  $('detail-content').querySelectorAll('[data-metric-edit]').forEach(button=>button.onclick=()=>showMetricForm(button.dataset.metricEdit,false,roomId));
  $('detail-content').querySelectorAll('[data-metric-remove]').forEach(button=>button.onclick=()=>apply(row.filter(item=>item.type!==button.dataset.metricRemove)));
  $('metric-add').onclick=()=>showMetricPicker(roomId);
  $('metric-close').onclick=()=>$('detail-dialog').close();$('metric-done').onclick=()=>$('detail-dialog').close();
  if(!$('detail-dialog').open)$('detail-dialog').showModal();focusDialogWithoutInput();
}
function showMetricPicker(roomId=metricEditorRoomId){
  metricEditorRoomId=String(roomId);$('detail-close').hidden=true;
  const used=new Set(metricRow(roomId).map(item=>item.type));
  $('detail-content').innerHTML=`<h2>Thêm cảm biến</h2><div class="device-type-list metric-type-list">${['co2','voc','temperature','humidity','pm25','air'].map(type=>`<button type="button" data-metric-type="${type}" ${used.has(type)?'disabled':''}><span class="${METRIC_TYPES[type].color}">${metricIcon(type)}</span><b>${METRIC_TYPES[type].label}</b></button>`).join('')}</div><div class="room-manager-actions"><button id="metric-back" type="button">Quay lại</button><button id="metric-picker-close" type="button">Đóng</button></div>`;
  $('detail-content').querySelectorAll('[data-metric-type]').forEach(button=>button.onclick=()=>showMetricForm(button.dataset.metricType,true,roomId));
  $('metric-back').onclick=()=>showMetricManager(null,roomId);$('metric-picker-close').onclick=()=>$('detail-dialog').close();
  focusDialogWithoutInput();
}
function showMetricForm(type,adding=false,roomId=metricEditorRoomId){
  if(!Object.hasOwn(METRIC_TYPES,type))return;
  metricEditorRoomId=String(roomId);$('detail-close').hidden=true;
  const key=metricEntityKeys(roomId)[type],item=metricRow(roomId).find(value=>value.type===type),name=item?.name||METRIC_TYPES[type].label;
  $('detail-content').innerHTML=`<h2>${adding?'Thêm':'Chỉnh sửa'} ${escapeHtml(METRIC_TYPES[type].label)}</h2><form id="metric-edit-form" class="new-device-form"><label>Tên hiển thị<input id="metric-edit-name" required maxlength="80" autocomplete="off" value="${escapeHtml(name)}"></label><label>Entity Home Assistant <small>(có thể bỏ qua)</small><input id="metric-edit-entity" list="entity-list" autocomplete="off" placeholder="Chọn hoặc nhập entity" value="${escapeHtml(config.entities[key]||'')}"></label><div><button id="metric-edit-cancel" type="button">Hủy</button><button type="submit">${adding?'Thêm cảm biến':'Lưu thay đổi'}</button></div></form>`;
  $('metric-edit-cancel').onclick=()=>showMetricManager(type,roomId);
  $('metric-edit-form').onsubmit=e=>{
    e.preventDefault();const next=metricRow(roomId),name=$('metric-edit-name').value.trim();if(!name)return;
    const index=next.findIndex(value=>value.type===type),item={type,name};
    if(index<0)next.push(item);else next[index]=item;
    config.entities[key]=$('metric-edit-entity').value.trim();persistConfig();saveMetricRow(roomId,next);showMetricManager(type,roomId);
  };
  focusDialogWithoutInput();
}

function renderDevices(){
  cancelLayoutLongPress();
  const view=document.createElement('div');view.className='device-view';
  const layout=currentDeviceLayout();
  if(!layout.length){const e=document.createElement('div');e.className='empty-message';e.textContent='Chưa có thiết bị';view.append(e)}
  for(const pos of layout){if(!DEVICE[pos.key])continue;const def={...DEVICE[pos.key],name:layoutEdits.names[pos.key]||DEVICE[pos.key].name};view.append(makeDeviceCard(def,pos))}
  if(layoutEditMode){view.classList.add('layout-editing');const actions=document.createElement('div');actions.className='layout-actions';const add=document.createElement('button');add.type='button';add.className='layout-add';add.textContent='+ Thiết bị';add.onclick=showAddDevicePicker;const done=document.createElement('button');done.type='button';done.className='layout-done';done.textContent='Xong';done.onclick=()=>{layoutEditMode=false;renderDevices()};actions.append(add,done);view.append(actions)}else installLayoutLongPress(view);
  $('content').replaceChildren(view);
}

function makeDeviceCard(def,pos){
  const displaySize=cardSizeFor(def,pos.size),card=document.createElement('article');card.className=`device-card ${displaySize}`;card.style.left=`${pos.x}px`;card.style.top=`${pos.y}px`;card.dataset.device=def.entity;card.dataset.layoutKey=pos.key;
  card.classList.add(`${def.kind}-card`);
  if(['washer','dryer','water','induction'].includes(def.kind))card.classList.add('appliance-card');
  if(isOn(def.entity))card.classList.add('active');if(!usable(def.entity))card.classList.add('disabled');
  if(def.kind==='binary'&&isOn(def.entity))card.classList.add('alert');
  if(def.kind==='toggle'){
    card.setAttribute('role','button');card.tabIndex=0;card.innerHTML=`<span class="device-icon">${iconSvg(def.icon)}</span><strong class="device-name">${escapeHtml(def.name)}</strong>`;
    card.onclick=()=>call(def.entity,'toggle');card.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();card.click()}};
  } else if(def.kind==='binary'){
    card.innerHTML=`<span class="device-icon">${iconSvg(def.icon)}</span><strong class="device-name">${escapeHtml(def.name)}</strong>`;
  } else if(def.kind==='ac') renderAcCard(card,def);
  else if(def.kind==='fan') renderFanCard(card,def);
  else if(def.kind==='cover') renderCoverCard(card,def);
  else if(def.kind==='cob') renderCobCard(card,def);
  else if(def.kind==='washer') renderWasherCard(card,def);
  else if(def.kind==='dryer') renderDryerCard(card,def);
  else if(def.kind==='water') renderWaterCard(card,def);
  else if(def.kind==='induction') renderInductionCard(card,def);
  else if(def.kind==='sensor') renderGenericSensor(card,def);
  if(layoutEditMode)enableCardEditing(card,pos,def);
  return card;
}

function installLayoutLongPress(view){
  let timer=null,startX=0,startY=0;
  const cancel=()=>{clearTimeout(timer);timer=null;if(layoutHoldCancel===cancel)layoutHoldCancel=null};
  view.onpointerdown=e=>{
    if(e.target!==view||e.isPrimary===false||e.button>0||modalOpen()||entityHoldCard||openingHoldPointer!==null||layoutEditMode)return;
    e.preventDefault();cancelLayoutLongPress();clearTextSelection();
    startX=e.clientX;startY=e.clientY;layoutHoldCancel=cancel;view.setPointerCapture?.(e.pointerId);
    timer=setTimeout(()=>{
      const valid=view.isConnected&&timer!==null&&!modalOpen()&&!entityHoldCard&&openingHoldPointer===null;
      cancel();if(!valid)return;
      layoutEditMode=true;renderDevices();toast('Kéo thẻ đến ô trống · chạm bút để đổi tên');
    },3000);
  };
  view.onpointermove=e=>{if(timer&&Math.hypot(e.clientX-startX,e.clientY-startY)>10)cancel()};
  for(const name of ['pointerup','pointercancel','pointerleave','lostpointercapture'])view.addEventListener(name,cancel);
  view.oncontextmenu=e=>e.preventDefault();
}
function enableCardEditing(card,pos,def){
  card.classList.add('layout-edit-card');card.onclick=null;card.onkeydown=null;const edit=document.createElement('button');edit.type='button';edit.className='card-edit-name';edit.setAttribute('aria-label','Sửa tên và entity');edit.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m5 16-1 4 4-1L19 8l-3-3zM14 7l3 3"/></svg>';edit.onpointerdown=e=>e.stopPropagation();edit.onclick=e=>{e.stopPropagation();showEditDeviceForm(pos.key,def)};const remove=document.createElement('button');remove.type='button';remove.className='card-remove';remove.setAttribute('aria-label','Xóa thiết bị khỏi Dashboard');remove.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M6 6l12 12M18 6 6 18"/></svg>';remove.onpointerdown=e=>e.stopPropagation();remove.onclick=e=>{e.stopPropagation();if(!confirm(`Xóa ${layoutEdits.names[pos.key]||def.name} khỏi Dashboard và Cài đặt entity?`))return;if(layoutEdits.customDevices[pos.key])removeCustomDevice(pos.key,def);else layoutEdits.removedDevices=[...new Set([...(layoutEdits.removedDevices||[]),pos.key])];saveLayoutEdits();renderDevices()};card.append(edit,remove);
  let dragging=false,dx=0,dy=0;card.onpointerdown=e=>{if(e.target.closest('.card-edit-name,.card-remove'))return;e.preventDefault();dragging=true;const rect=card.getBoundingClientRect(),scale=rect.width/card.offsetWidth;dx=(e.clientX-rect.left)/scale;dy=(e.clientY-rect.top)/scale;card.setPointerCapture?.(e.pointerId);card.classList.add('dragging')};card.onpointermove=e=>{if(!dragging)return;const rect=card.parentElement.getBoundingClientRect(),scale=rect.width/card.parentElement.offsetWidth;card.style.left=`${Math.max(0,(e.clientX-rect.left)/scale-dx)}px`;card.style.top=`${Math.max(0,(e.clientY-rect.top)/scale-dy)}px`};card.onpointerup=()=>{if(!dragging)return;dragging=false;card.classList.remove('dragging');const x=Math.round(parseFloat(card.style.left)/96)*96,y=Math.round(parseFloat(card.style.top)/96)*96;if(validDrop(card,x,y)){layoutEdits.positions[layoutKey()]={...(layoutEdits.positions[layoutKey()]||{}),[pos.key]:{x,y}};saveLayoutEdits()}else toast('Ô này không đủ chỗ cho thiết bị');renderDevices()};card.onpointercancel=()=>{dragging=false;renderDevices()};
}
function removeCustomDevice(key,def){
  const entityKeys=[def.entity,def.remainingEntity,def.finishEntity,def.powerEntity,def.dailyEntity,def.monthEntity,def.totalEntity].filter(Boolean);
  for(const page of Object.keys(layoutEdits.added||{}))delete layoutEdits.added[page][key];
  for(const page of Object.keys(layoutEdits.positions||{}))delete layoutEdits.positions[page][key];
  for(const page of Object.keys(layoutEdits.hidden||{}))layoutEdits.hidden[page]=(layoutEdits.hidden[page]||[]).filter(item=>item!==key);
  delete layoutEdits.customDevices[key];delete layoutEdits.names[key];delete layoutEdits.icons[key];delete DEVICE[key];
  if(config.rgbPresets)delete config.rgbPresets[def.entity];
  if(config.lightColorTypes)delete config.lightColorTypes[def.entity];
  if(config.lastLightColors)delete config.lastLightColors[def.entity];
  for(const entityKey of entityKeys){delete config.entities[entityKey];delete ENTITY_LABELS[entityKey];delete layoutEdits.names[entityKey]}
  persistConfig();
}
function validDrop(card,x,y){const width=card.offsetWidth,height=card.offsetHeight;if(x<0||y<0||x+width>856||y+height>376)return false;return ![...card.parentElement.querySelectorAll('.device-card')].some(other=>other!==card&&x<other.offsetLeft+other.offsetWidth&&x+width>other.offsetLeft&&y<other.offsetTop+other.offsetHeight&&y+height>other.offsetTop)}
function cardSizeFor(def,size){const kind=def?.kind;return kind==='binary'||kind==='dryer'||kind==='induction'?'compact':kind==='washer'||kind==='water'?'medium':size}
function deviceSize(key){for(const nav of Object.values(LAYOUTS))for(const room of nav){const found=room.find(pos=>pos.key===key);if(found)return cardSizeFor(DEVICE[key],found.size)}return cardSizeFor(DEVICE[key],'compact')}
function sizePixels(size){return size==='large'?[184,184]:size==='wide'?[280,88]:size==='medium'?[184,88]:[88,88]}
function findFreePosition(layout,size){const [width,height]=sizePixels(size);for(let y=0;y<=376-height;y+=96)for(let x=0;x<=856-width;x+=96){const free=layout.every(pos=>{const [w,h]=sizePixels(cardSizeFor(DEVICE[pos.key],pos.size));return !(x<pos.x+w&&x+width>pos.x&&y<pos.y+h&&y+height>pos.y)});if(free)return {x,y,size}}return null}
function showAddDevicePicker(){$('detail-close').hidden=false;const types=[['switch','Công tắc','switch','toggle','compact'],['sensor','Cảm biến','door','binary','compact'],['ac','Máy lạnh','snow','ac','large'],['fan','Quạt','fan','fan','large'],['cover','Rèm','cover','cover','wide'],['dimmer','Đèn điều chỉnh','light','cob','wide'],['washer','Máy giặt','washer','washer','medium'],['dryer','Máy sấy','dryer','dryer','compact'],['induction','Bếp từ','induction','induction','compact'],['water','Máy lọc nước','purifier','water','medium']];$('detail-content').innerHTML=`<h2>Chọn loại thiết bị</h2><div class="device-type-list">${types.map(([type,label,icon])=>`<button type="button" data-device-type="${type}"><span>${iconSvg(icon)}</span><b>${label}</b></button>`).join('')}</div>`;$('detail-content').querySelectorAll('[data-device-type]').forEach(button=>button.onclick=()=>{const selected=types.find(item=>item[0]===button.dataset.deviceType);selected[0]==='washer'?showNewWasherForm(selected):['dryer','induction','water'].includes(selected[0])?showNewStatsDeviceForm(selected):showNewDeviceForm(selected)});const dialog=$('detail-dialog');if(!dialog.open)dialog.showModal();focusDialogWithoutInput()}
function iconPickerHtml(title,choices,current){return `<fieldset class="sensor-icon-picker"><legend>${title}</legend>${choices.map(([value,text])=>`<button type="button" data-picker-icon="${value}" class="${value===current?'selected':''}"><span>${iconSvg(value)}</span><small>${text}</small></button>`).join('')}</fieldset>`}
function bindIconPicker(root,onSelect){root.querySelectorAll('[data-picker-icon]').forEach(button=>button.onclick=()=>{root.querySelectorAll('[data-picker-icon]').forEach(item=>item.classList.toggle('selected',item===button));onSelect(button.dataset.pickerIcon)})}
function entityDomains(type){return type==='cover'?['cover']:type==='ac'?['climate']:type==='fan'?['fan']:type==='dimmer'?['light']:type==='sensor'?['binary_sensor','sensor']:['light','switch','fan']}
function entityOptions(domains){return Object.values(states).filter(s=>domains.includes(s.entity_id.split('.')[0])).map(s=>`<option value="${escapeHtml(s.entity_id)}">${escapeHtml(s.attributes?.friendly_name||s.entity_id)}</option>`).join('')}
function showNewDeviceForm([type,label,icon,kind,size]){const choices=type==='sensor'?SENSOR_ICON_CHOICES:type==='switch'?LIGHT_ICON_CHOICES:null,domains=entityDomains(type);let selectedIcon=icon;$('detail-close').hidden=true;$('detail-content').innerHTML=`<h2>Thêm ${label.toLocaleLowerCase('vi')}</h2><form id="new-device-form" class="new-device-form"><label>Tên thiết bị<input id="new-device-name" required autocomplete="off" placeholder="Nhập tên hiển thị"></label><label>Entity Home Assistant <small>(có thể bỏ qua)</small><input id="new-device-entity" list="new-device-entity-list" autocomplete="off" placeholder="Chọn ${domains.map(d=>d+'.*').join(' / ')}"><datalist id="new-device-entity-list">${entityOptions(domains)}</datalist></label>${choices?iconPickerHtml(type==='sensor'?'Icon cảm biến':'Icon công tắc',choices,selectedIcon):''}<div><button type="button" id="new-device-back">Quay lại</button><button type="button" id="new-device-close">Đóng</button><button class="primary" type="submit">Thêm thiết bị</button></div></form>`;$('new-device-back').onclick=showAddDevicePicker;$('new-device-close').onclick=()=>$('detail-dialog').close();bindIconPicker($('detail-content'),value=>selectedIcon=value);focusDialogWithoutInput();$('new-device-form').onsubmit=e=>{e.preventDefault();const name=$('new-device-name').value.trim(),entity=$('new-device-entity').value.trim(),domain=entity.split('.')[0];if(entity&&!domains.includes(domain)){toast(`Entity ${label} phải thuộc: ${domains.map(d=>d+'.*').join(', ')}`);return}const key=`custom_${type}_${Date.now()}`,spot=findFreePosition(currentDeviceLayout(),size);if(!spot){toast('Không có ô trống đủ kích thước');return}const def={entity:key,name,icon:selectedIcon,kind};DEVICE[key]=def;ENTITY_LABELS[key]=name;layoutEdits.customDevices[key]=def;layoutEdits.names[key]=name;layoutEdits.icons[key]=selectedIcon;const page=layoutKey();layoutEdits.added[page]={...(layoutEdits.added[page]||{}),[key]:spot};config.entities[key]=entity;localStorage.setItem('ken-config-v3',JSON.stringify(config));saveLayoutEdits();$('detail-dialog').close();renderDevices()}}
function showEditDeviceForm(key,def){const entityKey=def.entity,choices=def.kind==='binary'?SENSOR_ICON_CHOICES:LIGHT_ICON_CHOICES.some(([value])=>value===def.icon)?LIGHT_ICON_CHOICES:null;let selectedIcon=def.icon;$('detail-close').hidden=true;$('detail-content').innerHTML=`<h2>Chỉnh sửa thiết bị</h2><form id="edit-device-form" class="new-device-form"><label>Tên thiết bị<input id="edit-device-name" required autocomplete="off" value="${escapeHtml(layoutEdits.names[key]||def.name)}"></label><label>Entity Home Assistant<input id="edit-device-entity" list="entity-list" autocomplete="off" placeholder="Chọn hoặc nhập entity" value="${escapeHtml(config.entities[entityKey]||'')}"></label>${choices?iconPickerHtml(def.kind==='binary'?'Icon cảm biến':'Icon đèn',choices,selectedIcon):''}<div><button type="button" id="edit-device-cancel">Hủy</button><button type="submit">Lưu thay đổi</button></div></form>`;bindIconPicker($('detail-content'),value=>selectedIcon=value);$('edit-device-cancel').onclick=()=>$('detail-dialog').close();$('edit-device-form').onsubmit=e=>{e.preventDefault();const name=$('edit-device-name').value.trim();layoutEdits.names[key]=name;layoutEdits.icons[key]=selectedIcon;DEVICE[key].icon=selectedIcon;ENTITY_LABELS[entityKey]=name;config.entities[entityKey]=$('edit-device-entity').value.trim();if(layoutEdits.customDevices[key]){layoutEdits.customDevices[key].name=name;layoutEdits.customDevices[key].icon=selectedIcon}saveLayoutEdits();localStorage.setItem('ken-config-v3',JSON.stringify(config));$('detail-dialog').close();renderDevices();toast('Đã lưu tên, entity và icon')};$('detail-dialog').showModal();focusDialogWithoutInput()}
function statsFieldsHtml(prefix,kind,def={}){const water=kind==='water',fields=water?[['daily','Hôm nay',def.dailyEntity],['month','Tháng này',def.monthEntity],['total','Tổng',def.totalEntity]]:[['power','Công suất hiện tại',def.powerEntity],['month','Tháng này',def.monthEntity],['total','Tổng tiêu thụ',def.totalEntity]];return fields.map(([id,label,key])=>`<label>${label}<input id="${prefix}-${id}" list="entity-list" autocomplete="off" placeholder="sensor.*" value="${escapeHtml(key?config.entities[key]||'':'')}"></label>`).join('')}
function showNewStatsDeviceForm([type,label,icon,kind,size]){$('detail-close').hidden=true;$('detail-content').innerHTML=`<h2>Thêm ${label.toLocaleLowerCase('vi')}</h2><form id="new-stats-device-form" class="new-device-form"><h3 class="entity-group-title">Cấu hình entity ${label}</h3><label>Tên thiết bị<input id="new-stats-name" required autocomplete="off" placeholder="${label}"></label><label>${kind==='water'?'Lưu lượng / trạng thái':'Trạng thái'}<input id="new-stats-status" list="entity-list" autocomplete="off" placeholder="Chọn entity"></label>${statsFieldsHtml('new-stats',kind)}<div><button type="button" id="new-stats-back">Quay lại</button><button type="button" id="new-stats-close">Đóng</button><button type="submit">Thêm thiết bị</button></div></form>`;$('new-stats-back').onclick=showAddDevicePicker;$('new-stats-close').onclick=()=>$('detail-dialog').close();$('new-stats-device-form').onsubmit=e=>{e.preventDefault();const name=$('new-stats-name').value.trim(),key=`custom_${type}_${Date.now()}`,spot=findFreePosition(currentDeviceLayout(),size);if(!spot){toast('Không có ô trống đủ kích thước');return}const def={entity:key,name,icon,kind},items=kind==='water'?[['dailyEntity','daily','Hôm nay'],['monthEntity','month','Tháng này'],['totalEntity','total','Tổng']]:[['powerEntity','power','Công suất hiện tại'],['monthEntity','month','Tháng này'],['totalEntity','total','Tổng tiêu thụ']];config.entities[key]=$('new-stats-status').value.trim();ENTITY_LABELS[key]=`${name} · Trạng thái`;for(const [prop,id,text] of items){const entityKey=`${key}_${id}`;def[prop]=entityKey;config.entities[entityKey]=$(`new-stats-${id}`).value.trim();ENTITY_LABELS[entityKey]=`${name} · ${text}`}DEVICE[key]=def;layoutEdits.customDevices[key]=def;layoutEdits.names[key]=name;layoutEdits.added[layoutKey()]={...(layoutEdits.added[layoutKey()]||{}),[key]:spot};localStorage.setItem('ken-config-v3',JSON.stringify(config));saveLayoutEdits();$('detail-dialog').close();renderDevices()};$('detail-dialog').showModal();focusDialogWithoutInput()}
function showEditStatsDeviceForm(key,def){$('detail-close').hidden=true;$('detail-content').innerHTML=`<h2>Chỉnh sửa ${escapeHtml(def.name)}</h2><form id="edit-stats-device-form" class="new-device-form"><h3 class="entity-group-title">Cấu hình entity thiết bị</h3><label>Tên thiết bị<input id="edit-stats-name" required autocomplete="off" value="${escapeHtml(layoutEdits.names[key]||def.name)}"></label><label>${def.kind==='water'?'Lưu lượng / trạng thái':'Trạng thái'}<input id="edit-stats-status" list="entity-list" autocomplete="off" value="${escapeHtml(config.entities[def.entity]||'')}"></label>${statsFieldsHtml('edit-stats',def.kind,def)}<div><button id="edit-stats-cancel" type="button">Hủy</button><button type="submit">Lưu thay đổi</button></div></form>`;$('edit-stats-cancel').onclick=()=>$('detail-dialog').close();$('edit-stats-device-form').onsubmit=e=>{e.preventDefault();const name=$('edit-stats-name').value.trim(),items=def.kind==='water'?[['dailyEntity','daily','Hôm nay'],['monthEntity','month','Tháng này'],['totalEntity','total','Tổng']]:[['powerEntity','power','Công suất hiện tại'],['monthEntity','month','Tháng này'],['totalEntity','total','Tổng tiêu thụ']];layoutEdits.names[key]=name;config.entities[def.entity]=$('edit-stats-status').value.trim();ENTITY_LABELS[def.entity]=`${name} · Trạng thái`;for(const [prop,id,text] of items){config.entities[def[prop]]=$(`edit-stats-${id}`).value.trim();ENTITY_LABELS[def[prop]]=`${name} · ${text}`}if(layoutEdits.customDevices[key])layoutEdits.customDevices[key].name=name;saveLayoutEdits();localStorage.setItem('ken-config-v3',JSON.stringify(config));$('detail-dialog').close();renderDevices();toast('Đã lưu các entity thiết bị')};$('detail-dialog').showModal();focusDialogWithoutInput()}
function showNewWasherForm([type,label,icon,kind,size]){$('detail-close').hidden=true;$('detail-content').innerHTML=`<h2>Thêm máy giặt</h2><form id="new-washer-form" class="new-device-form"><h3 class="entity-group-title">Cấu hình entity máy giặt</h3><label>Tên thiết bị<input id="new-washer-name" required autocomplete="off" placeholder="Máy giặt"></label><label>Trạng thái<input id="new-washer-status" list="new-washer-switches" autocomplete="off" placeholder="switch.*"><datalist id="new-washer-switches">${entityOptions(['switch'])}</datalist></label><label>Thời gian còn lại<input id="new-washer-remaining" list="new-washer-sensors" autocomplete="off" placeholder="sensor.*"><datalist id="new-washer-sensors">${entityOptions(['sensor'])}</datalist></label><label>Máy giặt xong<input id="new-washer-finish" list="new-washer-binary" autocomplete="off" placeholder="binary_sensor.*"><datalist id="new-washer-binary">${entityOptions(['binary_sensor'])}</datalist></label>${statsFieldsHtml('new-washer','washer')}<div><button type="button" id="new-washer-back">Quay lại</button><button type="button" id="new-washer-close">Đóng</button><button type="submit">Thêm thiết bị</button></div></form>`;$('new-washer-back').onclick=showAddDevicePicker;$('new-washer-close').onclick=()=>$('detail-dialog').close();$('new-washer-form').onsubmit=e=>{e.preventDefault();const name=$('new-washer-name').value.trim(),status=$('new-washer-status').value.trim(),remaining=$('new-washer-remaining').value.trim(),finish=$('new-washer-finish').value.trim();if(status&&!status.startsWith('switch.')){toast('Trạng thái phải là switch.*');return}if(remaining&&!remaining.startsWith('sensor.')){toast('Thời gian còn lại phải là sensor.*');return}if(finish&&!finish.startsWith('binary_sensor.')){toast('Máy giặt xong phải là binary_sensor.*');return}const key=`custom_washer_${Date.now()}`,remainingKey=`${key}_remaining`,finishKey=`${key}_finish`,powerKey=`${key}_power`,monthKey=`${key}_month`,totalKey=`${key}_total`,spot=findFreePosition(currentDeviceLayout(),size);if(!spot){toast('Không có ô trống đủ kích thước');return}const def={entity:key,remainingEntity:remainingKey,finishEntity:finishKey,powerEntity:powerKey,monthEntity:monthKey,totalEntity:totalKey,name,icon,kind};DEVICE[key]=def;for(const [entityKey,text] of [[key,'Trạng thái'],[remainingKey,'Thời gian còn lại'],[finishKey,'Máy giặt xong'],[powerKey,'Công suất hiện tại'],[monthKey,'Tháng này'],[totalKey,'Tổng tiêu thụ']])ENTITY_LABELS[entityKey]=`${name} · ${text}`;layoutEdits.customDevices[key]=def;layoutEdits.names[key]=name;const page=layoutKey();layoutEdits.added[page]={...(layoutEdits.added[page]||{}),[key]:spot};config.entities[key]=status;config.entities[remainingKey]=remaining;config.entities[finishKey]=finish;config.entities[powerKey]=$('new-washer-power').value.trim();config.entities[monthKey]=$('new-washer-month').value.trim();config.entities[totalKey]=$('new-washer-total').value.trim();localStorage.setItem('ken-config-v3',JSON.stringify(config));saveLayoutEdits();$('detail-dialog').close();renderDevices()};focusDialogWithoutInput()}
function showEditWasherForm(key,def){const statusKey=def.entity,remainingKey=def.remainingEntity||'washerRemaining',finishKey=def.finishEntity||'washerFinish',powerKey=def.powerEntity||'washerPower',monthKey=def.monthEntity||'washerMonthly',totalKey=def.totalEntity||'washerTotal',statsDef={...def,powerEntity:powerKey,monthEntity:monthKey,totalEntity:totalKey};$('detail-close').hidden=true;$('detail-content').innerHTML=`<h2>Chỉnh sửa máy giặt</h2><form id="edit-washer-form" class="new-device-form"><h3 class="entity-group-title">Cấu hình entity máy giặt</h3><label>Tên thiết bị<input id="edit-washer-name" required autocomplete="off" value="${escapeHtml(layoutEdits.names[key]||def.name)}"></label><label>Trạng thái<input id="edit-washer-status" list="entity-list" autocomplete="off" value="${escapeHtml(config.entities[statusKey]||'')}"></label><label>Thời gian còn lại<input id="edit-washer-remaining" list="entity-list" autocomplete="off" value="${escapeHtml(config.entities[remainingKey]||'')}"></label><label>Máy giặt xong<input id="edit-washer-finish" list="entity-list" autocomplete="off" value="${escapeHtml(config.entities[finishKey]||'')}"></label>${statsFieldsHtml('edit-washer','washer',statsDef)}<div><button id="edit-washer-cancel" type="button">Hủy</button><button type="submit">Lưu thay đổi</button></div></form>`;$('edit-washer-cancel').onclick=()=>$('detail-dialog').close();$('edit-washer-form').onsubmit=e=>{e.preventDefault();const name=$('edit-washer-name').value.trim();layoutEdits.names[key]=name;for(const [entityKey,id,text] of [[statusKey,'status','Trạng thái'],[remainingKey,'remaining','Thời gian còn lại'],[finishKey,'finish','Máy giặt xong'],[powerKey,'power','Công suất hiện tại'],[monthKey,'month','Tháng này'],[totalKey,'total','Tổng tiêu thụ']]){config.entities[entityKey]=$(`edit-washer-${id}`).value.trim();ENTITY_LABELS[entityKey]=`${name} · ${text}`}if(layoutEdits.customDevices[key])layoutEdits.customDevices[key]={...layoutEdits.customDevices[key],name,powerEntity:powerKey,monthEntity:monthKey,totalEntity:totalKey};saveLayoutEdits();localStorage.setItem('ken-config-v3',JSON.stringify(config));$('detail-dialog').close();renderDevices();toast('Đã lưu các entity máy giặt')};$('detail-dialog').showModal();focusDialogWithoutInput()}
const showEditDeviceFormBase=showEditDeviceForm;showEditDeviceForm=(key,def)=>def.kind==='washer'?showEditWasherForm(key,def):['dryer','induction','water'].includes(def.kind)?showEditStatsDeviceForm(key,def):showEditDeviceFormBase(key,def);
function focusDialogWithoutInput(){cancelLayoutLongPress();cancelEntityLongPress();clearTextSelection();const dialog=$('detail-dialog');dialog.setAttribute('tabindex','-1');dialog.setAttribute('autofocus','');dialog.focus({preventScroll:true});requestAnimationFrame(()=>dialog.focus({preventScroll:true}))}
function showDisconnectConfirm(){$('detail-close').hidden=true;$('detail-content').innerHTML=`<div class="confirm-dialog"><h2>Hủy kết nối</h2><p>Bạn có chắc muốn hủy kết nối tới HASS?</p><div><button id="disconnect-yes" type="button">Có</button><button id="disconnect-exit" type="button">Thoát</button></div></div>`;$('disconnect-yes').onclick=()=>{$('detail-dialog').close();forgetToken()};$('disconnect-exit').onclick=()=>$('detail-dialog').close();$('detail-dialog').showModal();focusDialogWithoutInput()}
function showRoomManager(){$('detail-close').hidden=true;const up='<svg viewBox="0 0 24 24"><path d="m6 15 6-6 6 6"/></svg>',down='<svg viewBox="0 0 24 24"><path d="m6 9 6 6 6-6"/></svg>',pencil='<svg viewBox="0 0 24 24"><path d="m5 16-1 4 4-1L19 8l-3-3zM14 7l3 3"/></svg>';$('detail-content').innerHTML=`<h2>Sắp xếp phòng</h2><div class="room-manager">${rooms.map((room,index)=>`<div><b>${escapeHtml(room.name)}</b><button type="button" data-room-up="${index}" aria-label="Chuyển lên">${up}</button><button type="button" data-room-down="${index}" aria-label="Chuyển xuống">${down}</button><button type="button" data-room-rename="${index}" aria-label="Sửa tên">${pencil}</button><button type="button" data-room-remove="${index}" aria-label="Xóa phòng">×</button></div>`).join('')}</div><div class="room-manager-actions"><button id="room-add" type="button">+ Thêm phòng</button><button id="room-close" type="button">Đóng</button><button id="room-done" type="button">Xong</button></div>`;const keep=rooms[currentRoom]?.id,apply=()=>{saveLayoutEdits();currentRoom=Math.max(0,rooms.findIndex(room=>room.id===keep));buildNavigation();render();showRoomManager()};$('detail-content').querySelectorAll('[data-room-up]').forEach(button=>button.onclick=()=>{const i=Number(button.dataset.roomUp);if(i>0){[rooms[i-1],rooms[i]]=[rooms[i],rooms[i-1]];apply()}});$('detail-content').querySelectorAll('[data-room-down]').forEach(button=>button.onclick=()=>{const i=Number(button.dataset.roomDown);if(i<rooms.length-1){[rooms[i],rooms[i+1]]=[rooms[i+1],rooms[i]];apply()}});$('detail-content').querySelectorAll('[data-room-rename]').forEach(button=>button.onclick=()=>{const i=Number(button.dataset.roomRename),name=prompt('Tên phòng',rooms[i].name);if(name?.trim()){rooms[i].name=name.trim();apply()}});$('detail-content').querySelectorAll('[data-room-remove]').forEach(button=>button.onclick=()=>{const i=Number(button.dataset.roomRemove);if(rooms.length===1){toast('Cần giữ lại ít nhất một phòng');return}if(confirm(`Xóa phòng ${rooms[i].name}?`)){rooms.splice(i,1);apply()}});$('room-add').onclick=()=>{const name=prompt('Tên phòng mới');if(name?.trim()){rooms.push({id:`custom-${Date.now()}`,name:name.trim(),source:null});saveLayoutEdits();buildNavigation();showRoomManager()}};$('room-close').onclick=()=>$('detail-dialog').close();$('room-done').onclick=()=>$('detail-dialog').close();const dialog=$('detail-dialog');if(!dialog.open)dialog.showModal();focusDialogWithoutInput()}
function renderGenericSensor(card,def){const s=state(def.entity);card.innerHTML=`<span class="device-icon">${iconSvg(def.icon)}</span><strong class="device-name">${escapeHtml(def.name)}</strong><span class="device-state">${usable(def.entity)?escapeHtml(`${s.state} ${s.attributes?.unit_of_measurement||''}`.trim()):'-'}</span>`}

function devicePowerHeader(def,label){return `<button class="device-power-zone" type="button" aria-label="${label}" aria-pressed="${isOn(def.entity)}"><span class="device-icon">${iconSvg(def.icon)}</span><strong class="device-name">${escapeHtml(def.name)}</strong></button>`}
function renderAcCard(card,def){
  const s=state(def.entity),target=Number(s?.attributes?.temperature);
  card.innerHTML=`${devicePowerHeader(def,'Bật hoặc tắt máy lạnh')}<div class="ac-temperature-controls"><strong class="ac-temperature-value" aria-label="Nhiệt độ cài đặt">${Number.isFinite(target)?formatNumber(target,1)+'°C':'--'}</strong><button type="button" data-step="-1" aria-label="Giảm nhiệt độ">−</button><button type="button" data-step="1" aria-label="Tăng nhiệt độ">+</button></div>`;
  card.querySelector('.device-power-zone').onclick=()=>call(def.entity,isOn(def.entity)?'turn_off':'turn_on');
  card.querySelectorAll('[data-step]').forEach(b=>b.onclick=()=>{if(!Number.isFinite(target))return;const min=Number(s?.attributes?.min_temp)||16,max=Number(s?.attributes?.max_temp)||30,step=Number(s?.attributes?.target_temp_step)||1;call(def.entity,'set_temperature',{temperature:Math.max(min,Math.min(max,target+Number(b.dataset.step)*step))})});
}
function renderFanCard(card,def){
  const s=state(def.entity),pct=Number(s?.attributes?.percentage);card.innerHTML=`${devicePowerHeader(def,'Bật hoặc tắt quạt')}<div class="large-main"><strong>${Number.isFinite(pct)?Math.round(pct)+'%':'--'}</strong></div><div class="mini-buttons"><button data-pct="33">1</button><button data-pct="66">2</button><button data-pct="100">3</button><button data-osc="1" aria-label="Đảo gió" aria-pressed="${Boolean(s?.attributes?.oscillating)}"><svg class="ui-icon oscillation-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 14a8 8 0 0 1 16 0M1 11l3 3 3-3M17 11l3 3 3-3"/></svg></button></div>`;
  card.querySelector('.device-power-zone').onclick=()=>call(def.entity,'toggle');card.querySelectorAll('[data-pct]').forEach(b=>b.onclick=()=>call(def.entity,'set_percentage',{percentage:Number(b.dataset.pct)}));card.querySelector('[data-osc]').onclick=()=>call(def.entity,'oscillate',{oscillating:!Boolean(s?.attributes?.oscillating)});
}
function renderCoverCard(card,def){
  const actual=Number(state(def.entity)?.attributes?.current_position),pending=optimisticControls[def.entity],pos=pending&&pending.until>Date.now()?pending.value:actual;card.innerHTML=`<button class="device-icon" type="button" aria-label="Mở hoặc đóng rèm">${iconSvg(def.icon)}</button><strong class="device-name">${escapeHtml(def.name)}</strong><span class="device-state">${Number.isFinite(pos)?Math.round(pos)+'%':'-'}</span><div class="control-strip cover-slider"><input type="range" min="0" max="100" value="${Number.isFinite(pos)?pos:0}" aria-label="Vị trí rèm"></div>`;
  const input=card.querySelector('input'),label=card.querySelector('.device-state');card.querySelector('.device-icon').onclick=()=>{const value=Number.isFinite(pos)&&pos>0?0:100;optimisticControls[def.entity]={value,until:Date.now()+10000};call(def.entity,'set_cover_position',{position:value})};input.onpointerdown=e=>{rangeDragging=true;input.setPointerCapture?.(e.pointerId)};input.oninput=e=>{const value=Math.round(Number(e.target.value));optimisticControls[def.entity]={value,until:Date.now()+10000};label.textContent=value+'%'};input.onchange=e=>{const value=Math.round(Number(e.target.value));optimisticControls[def.entity]={value,until:Date.now()+10000};call(def.entity,'set_cover_position',{position:value})};input.onpointerup=input.onpointercancel=()=>setTimeout(()=>{rangeDragging=false},800);
}
function dashboardLightPercent(key){
  const actual=lightDisplayPercent(key),pending=optimisticControls[key];
  if(state(key)?.state==='off'||(isOn(key)&&pending&&Math.abs(pending.value-actual)<=1))optimisticControls[key]=null;
  const current=optimisticControls[key];
  return current&&current.until>Date.now()?current.value:actual;
}
function syncDashboardLightCards(){
  if(rangeDragging||layoutEditMode)return;
  for(const card of document.querySelectorAll('[data-light-key]')){
    const key=card.dataset.lightKey,input=card.querySelector('input'),label=card.querySelector('.device-state');
    if(!input||!label)continue;
    const pct=dashboardLightPercent(key);
    input.value=String(pct);label.textContent=usable(key)?pct+'%':'-';
    card.classList.toggle('active',isOn(key));card.classList.toggle('disabled',!usable(key));
    card.style.setProperty('--device-light-color',lightDisplayColor(key));
  }
}
function renderCobCard(card,def){
  const s=state(def.entity),pct=dashboardLightPercent(def.entity);
  card.dataset.lightKey=def.entity;
  const modes=s?.attributes?.supported_color_modes||[],rgbLight=config.lightColorTypes?.[def.entity]==='rgb'||modes.some(mode=>['rgb','rgbw','rgbww','hs','xy'].includes(mode));
  card.classList.toggle('rgb-light',rgbLight);card.style.setProperty('--device-light-color',rgbLight?lightDisplayColor(def.entity):'');
  card.innerHTML=`<button class="device-icon" type="button" aria-label="Bật hoặc tắt đèn">${iconSvg(def.icon)}</button><strong class="device-name">${escapeHtml(def.name)}</strong><span class="device-state">${usable(def.entity)?pct+'%':'-'}</span><div class="control-strip cob-slider"><input type="range" min="0" max="100" value="${pct}" aria-label="Độ sáng"></div>`;
  let holdTimer;
  card.onpointerdown=e=>{if(e.target.closest('input,button'))return;e.preventDefault?.();e.stopPropagation?.();holdTimer=setTimeout(()=>{markHeldPopupGesture(e.pointerId);showCobDetail(def.entity,def.name)},600)};
  for(const event of ['pointerup','pointercancel','pointerleave'])card.addEventListener(event,()=>clearTimeout(holdTimer));
  card.querySelector('.device-icon').onclick=()=>call(def.entity,'toggle');
  const input=card.querySelector('input'),label=card.querySelector('.device-state');
  input.onpointerdown=e=>{rangeDragging=true;input.setPointerCapture?.(e.pointerId)};
  input.oninput=e=>{const value=Math.round(Number(e.target.value));optimisticControls[def.entity]={value,until:Date.now()+10000};label.textContent=value+'%'};
  input.onchange=e=>{const value=Math.round(Number(e.target.value));optimisticControls[def.entity]={value,until:Date.now()+10000};call(def.entity,value<=0?'turn_off':'turn_on',value<=0?{}:{brightness_pct:value})};
  input.onpointerup=input.onpointercancel=()=>setTimeout(()=>{rangeDragging=false;syncDashboardLightCards()},800);
}
function rgbToHex(rgb){return '#'+rgb.slice(0,3).map(value=>Math.max(0,Math.min(255,Math.round(Number(value)||0))).toString(16).padStart(2,'0')).join('')}
function hsToRgb([h,s]){s/=100;const c=s,x=c*(1-Math.abs(h/60%2-1)),m=1-c,rgb=h<60?[c,x,0]:h<120?[x,c,0]:h<180?[0,c,x]:h<240?[0,x,c]:h<300?[x,0,c]:[c,0,x];return rgb.map(value=>(value+m)*255)}
function cobCurrentColor(s){const mode=s?.attributes?.color_mode,rgb=s?.attributes?.rgb_color,hs=s?.attributes?.hs_color;if(mode!=='color_temp'&&Array.isArray(rgb)&&rgb.length>=3)return rgbToHex(rgb);if(mode!=='color_temp'&&Array.isArray(hs)&&hs.length>=2)return rgbToHex(hsToRgb(hs));const k=Number(s?.attributes?.color_temp_kelvin)||(Number(s?.attributes?.color_temp)?Math.round(1000000/Number(s.attributes.color_temp)):4000);return k<2850?'#ffd39a':k<3500?'#ffe0ad':k<5250?'#fff3d6':'#e8f2ff'}
function renderWasherCard(card,def){
  const statusKey=def.entity,remainingKey=def.remainingEntity||'washerRemaining',finishKey=def.finishEntity||'washerFinish',running=isOn(statusKey),finished=isOn(finishKey),remaining=formatRemaining(remainingKey),changed=state(finishKey)?.last_changed||state(finishKey)?.last_updated,finishTime=changed?new Date(changed).toLocaleTimeString('vi-VN',{hour:'2-digit',minute:'2-digit',hour12:false}):new Date().toLocaleTimeString('vi-VN',{hour:'2-digit',minute:'2-digit',hour12:false}),statusLine=finished?`<div class="data-line finish-notice"><span>Đã giặt xong lúc</span><b>${finishTime}</b></div>`:running?`<div class="data-line"><span>Thời gian còn lại</span><b class="washer-remaining-value" data-remaining-key="${escapeHtml(remainingKey)}">${escapeHtml(remaining)}</b></div>`:'';card.classList.toggle('alert',running&&!finished);card.classList.toggle('finished',finished);card.classList.toggle('running',running&&!finished);card.innerHTML=`<span class="device-icon">${applianceIconSvg('washer')}</span><strong class="device-name">${escapeHtml(def.name)}</strong><div class="washer-status-line">${statusLine}</div>`;card.onclick=()=>showDeviceDetail(def);
}
function renderDryerCard(card,def){const active=isOn(def.entity);card.classList.toggle('alert',active);card.classList.toggle('running',active);card.innerHTML=`<span class="device-icon">${applianceIconSvg('dryer')}</span><strong class="device-name">${escapeHtml(def.name)}</strong>`;card.onclick=()=>showDeviceDetail(def)}
function renderWaterCard(card,def){const dailyKey=def.dailyEntity||'waterToday',flow=numeric(def.entity),running=flow!=null&&flow>0;card.classList.toggle('alert',running);card.classList.toggle('running',running);card.innerHTML=`<span class="device-icon">${applianceIconSvg('water')}</span><strong class="device-name">${escapeHtml(def.name)}</strong><div class="washer-status-line"><div class="data-line"><span>Hôm nay</span><b>${escapeHtml(formatSensor(dailyKey,1))}</b></div></div>`;card.onclick=()=>showDeviceDetail(def)}
function renderInductionCard(card,def){const on=String(state(def.entity)?.state||'').toLowerCase();const active=!['','off','idle','unknown','unavailable','0'].includes(on);card.classList.toggle('alert',active);card.classList.toggle('running',active);card.innerHTML=`<span class="device-icon">${applianceIconSvg('induction')}</span><strong class="device-name">${escapeHtml(def.name)}</strong>`;card.onclick=()=>showDeviceDetail(def)}

function translateMode(mode){return ({off:'Tắt',cool:'Làm lạnh',heat:'Sưởi',auto:'Tự động',dry:'Hút ẩm',fan_only:'Chỉ quạt'})[mode]||mode}

async function call(key,service,data={}){
  resetIdle();
  if(!canControl(key)){toast('Thiết bị chưa sẵn sàng');return}
  const entity=config.entities[key]||'',requiredDomain=service==='set_cover_position'?'cover':service==='set_hvac_mode'||service==='set_temperature'?'climate':service==='set_percentage'||service==='oscillate'?'fan':null;if(requiredDomain&&!entity.startsWith(requiredDomain+'.')){toast(`Entity phải thuộc miền ${requiredDomain}.*`);return}
  if(demo){applyDemoAction(key,service,data);syncLightPopup();if(!rangeDragging)render();return}
  try{await client.call(config.entities[key],service,data)}catch(error){toast(error.message)}
}

function applyDemoAction(key,service,data){
  const s=state(key);if(!s)return;
  if(service==='toggle')s.state=isOn(key)?'off':'on';
  else if(service==='turn_on'){s.state='on';if(data.brightness_pct!=null)s.attributes.brightness=Math.round(data.brightness_pct*255/100);if(data.brightness!=null)s.attributes.brightness=data.brightness;if(data.color_temp_kelvin!=null){s.attributes.color_temp_kelvin=data.color_temp_kelvin;s.attributes.color_mode='color_temp'}if(Array.isArray(data.rgb_color)){s.attributes.rgb_color=data.rgb_color;s.attributes.color_mode='rgb'}}
  else if(service==='turn_off')s.state='off';
  else if(service==='set_temperature')s.attributes.temperature=data.temperature;
  else if(service==='set_hvac_mode')s.state=data.hvac_mode;
  else if(service==='set_percentage'){s.state='on';s.attributes.percentage=data.percentage}
  else if(service==='oscillate')s.attributes.oscillating=data.oscillating;
  else if(service==='set_cover_position'){s.attributes.current_position=data.position;s.state=data.position===0?'closed':'open'}
  else if(service==='open_cover'){s.attributes.current_position=100;s.state='open'}
  else if(service==='close_cover'){s.attributes.current_position=0;s.state='closed'}
  else if(service==='volume_set')s.attributes.volume_level=data.volume_level;
  else if(service==='media_play_pause')s.state=s.state==='playing'?'paused':'playing';
  else if(service==='volume_mute')s.attributes.is_volume_muted=data.is_volume_muted;
}

function renderMusic(){
  if(!config.usePhicomm&&musicSource!=='ma'){musicSource='ma';localStorage.setItem('ken-music-source','ma')}
  const s=state('music'),ha=s?.attributes||{},direct=musicSource==='aibox',a=direct?{media_title:directSpeakerState.title,media_artist:directSpeakerState.artist,media_album_name:'Điều khiển trực tiếp',volume_level:directSpeakerState.volume,media_position:directSpeakerState.position,media_duration:directSpeakerState.duration}:ha,playing=direct?directSpeakerState.playing:s?.state==='playing',volume=Number(a.volume_level),duration=Number(a.media_duration)||0,position=currentMediaPosition(s,a,playing,duration),art=!direct&&a.entity_picture?`<img src="${escapeHtml(mediaArtworkUrl(a.entity_picture))}" alt="Ảnh album">`:iconSvg('music');
  const page=document.createElement('div');page.className='page-panel music-page';page.innerHTML=`
    <section class="music-card music-head"><span class="blue">${iconSvg('music')}</span><strong>Phicomm R1</strong><span class="music-status">${direct?(directSpeakerStatus==='ready'?(playing?'Đang phát':'Đã kết nối'):'Chưa kết nối'):(usable('music')?escapeHtml(translateMediaState(s.state)):'-')}</span></section>
    <section class="music-card music-main"><div class="album-art">${art}</div><div class="track-info"><strong>${escapeHtml(a.media_title||'Chưa phát nhạc')}</strong><span>${escapeHtml(a.media_artist||'-')}</span><span>${escapeHtml(a.media_album_name||'-')}</span></div><div class="seek-control"><span id="seek-current">${formatPlaybackTime(position)}</span><input id="music-seek" type="range" min="0" max="${Math.max(1,duration)}" value="${Math.min(position,duration||position)}" ${duration>0?'':'disabled'} aria-label="Vị trí phát nhạc"><span>${formatPlaybackTime(duration)}</span></div><div class="transport"><button data-media="shuffle_set" aria-label="Phát ngẫu nhiên">${mediaIcon('shuffle')}</button><button data-media="media_previous_track" aria-label="Bài trước">${mediaIcon('previous')}</button><button class="play" data-media="media_play_pause" aria-label="${playing?'Tạm dừng':'Phát'}">${mediaIcon(playing?'pause':'play')}</button><button data-media="media_next_track" aria-label="Bài sau">${mediaIcon('next')}</button><button data-media="repeat_set" aria-label="Lặp lại">${mediaIcon('repeat')}</button></div></section>
    <aside class="music-card music-side"><div class="music-source"><button type="button" data-source-mode="ma" class="${direct?'':'selected'}">Music Assistant</button>${config.usePhicomm?`<button type="button" data-source-mode="aibox" class="${direct?'selected':''}">AI BOX</button>`:''}</div><p>${direct?'Kết nối '+escapeHtml(config.speakerAddress||'chưa cấu hình địa chỉ loa'):'Thư viện Music Assistant'}</p>${direct?`<div class="aibox-tabs"><button type="button" data-library="search" class="${directSpeakerState.libraryMode!=='playlists'?'selected':''}">Tìm bài</button><button type="button" data-library="playlists" class="${directSpeakerState.libraryMode==='playlists'?'selected':''}">Playlist</button></div><form class="aibox-search"><input type="search" placeholder="Tên bài hát..." aria-label="Tìm bài hát"><button type="submit">Tìm</button></form>`:`<div class="ma-tabs">${[['artists','Artists'],['albums','Albums'],['tracks','Tracks'],['playlists','Playlist']].map(([key,label])=>`<button type="button" data-ma-category="${key}" class="${haBrowse.category===key?'selected':''}">${label}</button>`).join('')}</div>`}<div class="playlist"></div></aside>
    <section class="music-card volume-card"><button data-mute="1" aria-label="${a.is_volume_muted||volume===0?'Bật âm thanh':'Tắt âm thanh'}">${mediaIcon(a.is_volume_muted||volume===0?'mute':'volume')}</button><button data-voldelta="-0.01">−</button><input type="range" min="0" max="100" value="${Number.isFinite(volume)?volume*100:0}" aria-label="Âm lượng"><button data-voldelta="0.01">+</button><b>${Number.isFinite(volume)?Math.round(volume*100)+'%':'-'}</b></section>`;
  const playlist=page.querySelector('.playlist');
  if(!direct){if(haBrowse.loading)playlist.innerHTML='<p>Đang tải thư viện…</p>';else if(haBrowse.items.length)for(const [index,item] of haBrowse.items.slice(0,7).entries()){const b=document.createElement('button');b.type='button';b.textContent=item.title||item.name||'Không tên';b.title=item.media_class||item.media_content_type||'';b.onclick=()=>openHABrowseItem(index);playlist.append(b)}else playlist.innerHTML='<p>Chọn nhóm để tải thư viện từ Home Assistant.</p>'}else{const items=directSpeakerState.libraryMode==='playlists'?directSpeakerState.playlists:directSpeakerState.results;if(items.length)for(const item of items.slice(0,5)){const b=document.createElement('button');b.type='button';b.textContent=item.name||item.title||'Không tên';b.title=item.artist||item.channel||'';b.onclick=()=>directSpeakerState.libraryMode==='playlists'?directSpeaker.playPlaylist(item.id||item.playlist_id):directSpeaker.playSong(item.video_id||item.id);playlist.append(b)}else playlist.innerHTML='<p>Nhập tên bài hát hoặc mở danh sách playlist.</p>'}
  page.querySelectorAll('[data-source-mode]').forEach(b=>b.onclick=()=>selectMusicSource(b.dataset.sourceMode));
  page.querySelectorAll('[data-ma-category]').forEach(b=>b.onclick=()=>loadHABrowseCategory(b.dataset.maCategory));
  page.querySelectorAll('[data-library]').forEach(b=>b.onclick=()=>{if(b.dataset.library==='playlists')directSpeaker.listPlaylists();else directSpeaker.showSearch()});
  const searchForm=page.querySelector('.aibox-search');if(searchForm)searchForm.onsubmit=e=>{e.preventDefault();directSpeaker.search(searchForm.querySelector('input').value)};
  page.querySelectorAll('[data-media]').forEach(b=>b.onclick=()=>{const service=b.dataset.media;if(direct){if(service==='media_play_pause')directSpeaker.playPause();else if(service==='media_previous_track')directSpeaker.previous();else if(service==='media_next_track')directSpeaker.next();else toast('Chế độ này chỉ hỗ trợ qua Music Assistant')}else if(service==='shuffle_set')call('music',service,{shuffle:!Boolean(ha.shuffle)});else if(service==='repeat_set')call('music',service,{repeat:ha.repeat==='all'?'off':'all'});else call('music',service)});
  page.querySelector('[data-mute]').onclick=()=>direct?directSpeaker.mute():call('music','volume_mute',{is_volume_muted:!Boolean(ha.is_volume_muted)});
  page.querySelectorAll('[data-voldelta]').forEach(b=>b.onclick=()=>{const next=Math.max(0,Math.min(1,(Number.isFinite(volume)?volume:0)+Number(b.dataset.voldelta)));direct?directSpeaker.setVolume(next):call('music','volume_set',{volume_level:next})});
  page.querySelector('.volume-card input').onchange=e=>direct?directSpeaker.setVolume(Number(e.target.value)/100):call('music','volume_set',{volume_level:Number(e.target.value)/100});
  const seek=page.querySelector('#music-seek');seek.oninput=e=>page.querySelector('#seek-current').textContent=formatPlaybackTime(Number(e.target.value));seek.onchange=e=>direct?directSpeaker.seek(Number(e.target.value)):call('music','media_seek',{seek_position:Number(e.target.value)});
  $('content').replaceChildren(page);
  if(!direct&&!haBrowse.loaded&&!haBrowse.loading&&connected)loadHABrowseCategory(haBrowse.category);
}
function formatPlaybackTime(value){const seconds=Math.max(0,Math.floor(Number(value)||0));return `${Math.floor(seconds/60)}:${String(seconds%60).padStart(2,'0')}`}
function currentMediaPosition(s,a,playing,duration){let position=Number(a.media_position)||0;if(playing&&a.media_position_updated_at){const elapsed=(Date.now()-new Date(a.media_position_updated_at).getTime())/1000;if(Number.isFinite(elapsed)&&elapsed>0)position+=elapsed}return Math.max(0,Math.min(duration||position,position))}
function mediaArtworkUrl(path){try{return new URL(path,config.url||location.origin).href}catch{return path}}
function selectMusicSource(source){if(!['ma','aibox'].includes(source)||source==='aibox'&&!config.usePhicomm)return;musicSource=source;localStorage.setItem('ken-music-source',source);if(source==='aibox'){try{directSpeaker.connect(config.speakerAddress)}catch(error){toast(error.message)}}else{directSpeaker.close();directSpeakerStatus='offline';const available=state('music')?.attributes?.source_list||[];const match=available.find(v=>/music assistant/i.test(v));if(match)call('music','select_source',{source:match})}renderMusic()}
async function browseHAMedia(item){const payload={type:'media_player/browse_media',entity_id:config.entities.music};if(item?.media_content_id!=null)payload.media_content_id=item.media_content_id;if(item?.media_content_type)payload.media_content_type=item.media_content_type;return client.request(payload)}
async function loadHABrowseCategory(category){haBrowse.category=category;haBrowse.loading=true;if(currentNav==='music'&&musicSource==='ma')renderMusic();try{if(!haBrowse.root)haBrowse.root=await browseHAMedia();const words={artists:/artist|nghệ sĩ/i,albums:/album/i,tracks:/track|bài hát|song/i,playlists:/playlist|danh sách/i},children=haBrowse.root?.children||[],node=children.find(item=>words[category].test(`${item.title||''} ${item.media_class||''} ${item.media_content_type||''}`));const result=node?.can_expand?await browseHAMedia(node):haBrowse.root;haBrowse.items=(result?.children||[]).filter(item=>{const text=`${item.media_class||''} ${item.media_content_type||''} ${item.title||''}`;return node||words[category].test(text)});haBrowse.loaded=true}catch(error){haBrowse.items=[];toast('Không tải được thư viện Music Assistant: '+error.message)}finally{haBrowse.loading=false;if(currentNav==='music'&&musicSource==='ma')renderMusic()}}
async function openHABrowseItem(index){const item=haBrowse.items[index];if(!item)return;if(item.can_play&&!item.can_expand){call('music','play_media',{media_content_id:item.media_content_id,media_content_type:item.media_content_type});return}if(item.can_expand){haBrowse.loading=true;renderMusic();try{const result=await browseHAMedia(item);haBrowse.items=result?.children||[]}catch(error){toast(error.message)}finally{haBrowse.loading=false;renderMusic()}}else if(item.media_content_id)call('music','play_media',{media_content_id:item.media_content_id,media_content_type:item.media_content_type})}
function translateMediaState(v){return ({playing:'Đang phát',paused:'Tạm dừng',idle:'Nghỉ',off:'Tắt',standby:'Chờ'})[v]||v}

function localDateKey(date){return `${date.getFullYear()}-${String(date.getMonth()+1).padStart(2,'0')}-${String(date.getDate()).padStart(2,'0')}`}
function updateHistory(){
  if(demo)return;
  const date=localDateKey(new Date()),month=date.slice(0,7);let total=numeric('totalEnergy'),changed=false;
  const set=(bucket,key,value)=>{if(history[bucket][key]!==value){history[bucket][key]=value;changed=true}};
  if(total!=null){const u=unit('totalEnergy');if(u==='Wh')total/=1000;else if(u==='MWh')total*=1000;if(history.lastTotal!=null&&total>=history.lastTotal){const delta=total-history.lastTotal;if(history.lastDate===date)set('days',date,(history.days[date]||0)+delta);if(history.lastMonth===month)set('months',month,(history.months[month]||0)+delta)}if(history.lastTotal!==total||history.lastDate!==date||history.lastMonth!==month){history.lastTotal=total;history.lastDate=date;history.lastMonth=month;changed=true}}
  const monthly=numeric('energy');if(monthly!=null)set('months',month,monthly);const bill=numeric('cost');if(bill!=null)set('bills',month,bill);
  if(changed)localStorage.setItem('ken-history-v3',JSON.stringify(history));
}
function renderEnergy(){
  const now=new Date(),year=now.getFullYear(),month=now.getMonth(),daysInMonth=new Date(year,month+1,0).getDate();
  const dayValues=Array.from({length:daysInMonth},(_,i)=>history.days[`${year}-${String(month+1).padStart(2,'0')}-${String(i+1).padStart(2,'0')}`]??null);
  const months=Array.from({length:12},(_,i)=>history.months[`${year}-${String(i+1).padStart(2,'0')}`]??null);
  const bills=Array.from({length:12},(_,i)=>{const v=history.bills[`${year}-${String(i+1).padStart(2,'0')}`];return v==null?null:v/1000});
  if(demo){for(let i=0;i<dayValues.length;i++)dayValues[i]=5+((i*7)%13);for(let i=0;i<12;i++){months[i]=i<=month?150+(i*37)%190:null;bills[i]=i<=month?450+(i*83)%620:null}}
  const page=document.createElement('div');page.className='page-panel energy-page';page.append(makeChart('Điện theo ngày','kWh',`${month+1} / ${year}`,dayValues,true,'daily'),makeChart('Điện theo tháng','kWh',String(year),months,false),makeChart('Tiền theo tháng','k VND',String(year),bills,false));$('content').replaceChildren(page);
}
function makeChart(title,unitText,period,values,sparse=false,extra=''){
  const panel=document.createElement('section');panel.className=`chart-panel ${extra}`;panel.innerHTML=`<strong class="chart-title">${title}</strong><span class="chart-unit">${unitText}</span><span class="chart-period">${period}</span>`;
  const valid=values.filter(v=>v!=null&&Number.isFinite(v));if(!valid.length){const empty=document.createElement('div');empty.className='chart-empty';empty.textContent='Chờ dữ liệu được lưu';panel.append(empty);return panel}
  const max=Math.max(1,...valid),chart=document.createElement('div');chart.className='bar-chart';values.forEach((v,i)=>{const wrap=document.createElement('div');wrap.className='bar-wrap';const bar=document.createElement('div');bar.className='bar';bar.style.height=v==null?'0':`${Math.max(1,v/max*100)}%`;bar.title=v==null?'':`${formatNumber(v,1)} ${unitText}`;wrap.append(bar);if(!sparse||i===0||(i+1)%5===0||i===values.length-1){const label=document.createElement('span');label.className='bar-label';label.textContent=String(i+1);wrap.append(label)}chart.append(wrap)});panel.append(chart);return panel;
}

function settingsEntityEntries(){const omitted=new Set(['dryerDaily','inductionDaily']),props=['entity','remainingEntity','finishEntity','powerEntity','dailyEntity','monthEntity','totalEntity'];for(const deviceKey of removedDeviceKeys()){const def=DEVICE[deviceKey];if(def)for(const prop of props)if(def[prop])omitted.add(def[prop])}const entries=entityCatalog().filter(([key])=>!omitted.has(key)),group=['washerActive','washerRemaining','washerFinish'].filter(key=>!omitted.has(key)),grouped=group.map(key=>[key,ENTITY_LABELS[key]]),rest=entries.filter(([key])=>!group.includes(key)),insertAt=Math.max(0,rest.findIndex(([key])=>key==='rain'));rest.splice(insertAt,0,...grouped);return rest}
function renderSettings(){
  const token=savedToken(),entityFields=settingsEntityEntries().map(([key,label])=>{const deviceKey=Object.keys(DEVICE).find(item=>DEVICE[item].entity===key),displayName=layoutEdits.names[key]||layoutEdits.names[deviceKey]||label;return `<label>${escapeHtml(displayName)}<input data-settings-entity="${escapeHtml(key)}" list="entity-list" autocomplete="off" value="${escapeHtml(config.entities[key]||'')}"></label>`}).join('');
  if(settingsEntityMode){
    const page=document.createElement('div');page.className='page-panel entity-page';page.innerHTML=`<form id="settings-entity-form" class="settings-card entity-settings"><div class="entity-page-head"><div><strong>Cài đặt entity</strong></div><div class="entity-page-actions"><button id="settings-entity-refresh" type="button">Làm mới entity</button><button type="submit">Lưu entity</button><button id="settings-entity-back" type="button">Quay lại</button></div></div><div class="settings-entity-grid">${entityFields}</div></form>`;
    page.querySelector('#settings-entity-refresh').onclick=refreshEntityStates;
    page.querySelector('#settings-entity-back').onclick=()=>{settingsEntityMode=false;renderSettings()};
    page.querySelector('#settings-entity-form').onsubmit=e=>{e.preventDefault();config={...config,entities:{...config.entities,...Object.fromEntries([...page.querySelectorAll('[data-settings-entity]')].map(input=>[input.dataset.settingsEntity,input.value.trim()||DEFAULT_ENTITIES[input.dataset.settingsEntity]]))}};persistConfig();settingsEntityMode=false;renderSettings();toast('Đã lưu tất cả entity')};
    $('content').replaceChildren(page);return;
  }
  const connectionPanel=`<form id="settings-ha-form" class="settings-card connection-settings">
      <div class="connection-head"><div><strong class="settings-title">Home Assistant</strong><span class="connection-state ${connected?'online':''}">${connected?'● Trực tuyến':demo?'● Bản mẫu':'● Ngoại tuyến'}</span></div></div>
      <div class="connection-fields"><label>Địa chỉ HA<input id="settings-ha-url" type="url" placeholder="http://192.168.1.10:8123" value="${escapeHtml(config.url)}"></label><label>Long-lived access token<input id="settings-ha-token" type="password" autocomplete="off" placeholder="${hasStoredToken()&&!token?'Nhập lại token cho địa chỉ này':'Dán token tại đây'}" value="${escapeHtml(token)}"></label><label>Địa chỉ loa Phicomm R1<input id="settings-speaker-address" type="url" inputmode="url" placeholder="http://192.168.1.236:8081/" value="${escapeHtml(config.speakerAddress)}"></label><label class="phicomm-option"><input id="settings-use-phicomm" type="checkbox" ${config.usePhicomm?'checked':''}><span>Dùng loa Phicomm và AI BOX</span></label></div>
      <div class="connection-actions"><button id="settings-full-config" type="button">Cài đặt entity</button><button id="settings-forget" type="button">Hủy kết nối</button><button class="primary" type="submit">Lưu & kết nối</button></div>
    </form>`;
  const page=document.createElement('div');page.className='page-panel settings-page';page.innerHTML=`
    <section class="settings-card display-settings"><div><strong class="settings-title">Độ sáng</strong><span class="setting-value" id="brightness-label">${displaySettings.brightness}%</span><input id="brightness-setting" type="range" min="20" max="100" value="${displaySettings.brightness}"></div><div><strong class="settings-title">Làm tối sau</strong><div class="timeout-buttons">${[-1,1,2,5,10].map(v=>`<button type="button" data-timeout="${v}" class="${v===displaySettings.timeout?'selected':''}">${v<0?'Luôn bật':v+' phút'}</button>`).join('')}</div></div><div class="theme-setting"><strong class="settings-title">Giao diện</strong><div><select id="theme-choice" aria-label="Chọn giao diện"><option value="default" ${config.theme==='default'?'selected':''}>Mặc định</option><option value="spring" ${config.theme==='spring'?'selected':''}>Mùa xuân</option><option value="summer" ${config.theme==='summer'?'selected':''}>Mùa hạ</option><option value="autumn" ${config.theme==='autumn'?'selected':''}>Mùa thu</option><option value="winter" ${config.theme==='winter'?'selected':''}>Mùa đông</option><option value="dark" ${config.theme==='dark'?'selected':''}>Giao diện tối</option></select><button id="theme-save" type="button">Lưu giao diện</button></div></div></section>
    <section class="settings-card system-settings" id="server-card"><div class="server-card-head"><span class="server-icon">${iconSvg('server')}</span><strong class="settings-title">Máy chủ Homeassistant</strong></div><div class="server-info"><span>${iconSvg('climate')}Nhiệt độ CPU</span><b data-server-value="piTemp">${escapeHtml(formatSensor('piTemp',1,'°C'))}</b><span>${iconSvg('disk')}Dung lượng trống</span><b data-server-value="piDisk">${escapeHtml(formatSensor('piDisk',1))}</b><span>${iconSvg('memory')}RAM</span><b data-server-value="piRam">${escapeHtml(formatSensor('piRam',1,'%'))}</b></div></section>
    ${connectionPanel}`;
  bindEntityHold(page.querySelector('#server-card'),()=> 'Máy chủ Homeassistant',showServerEntityEditor);
  page.querySelector('#brightness-setting').oninput=e=>{displaySettings.brightness=Number(e.target.value);page.querySelector('#brightness-label').textContent=displaySettings.brightness+'%';applyDisplaySettings()};
  page.querySelectorAll('[data-timeout]').forEach(b=>b.onclick=()=>{displaySettings.timeout=Number(b.dataset.timeout);applyDisplaySettings();renderSettings()});
  page.querySelector('#theme-save').onclick=()=>{config.theme=page.querySelector('#theme-choice').value;applyTheme();persistConfig();toast('Đã lưu giao diện '+page.querySelector('#theme-choice').selectedOptions[0].textContent)};
  page.querySelector('#settings-full-config').onclick=async()=>{settingsEntityMode=true;renderSettings();await refreshEntityStates()};
  page.querySelector('#settings-forget').onclick=showDisconnectConfirm;
  const phicommToggle=page.querySelector('#settings-use-phicomm'),speakerInput=page.querySelector('#settings-speaker-address');speakerInput.disabled=!phicommToggle.checked;phicommToggle.onchange=()=>{config.usePhicomm=phicommToggle.checked;speakerInput.disabled=!phicommToggle.checked;persistConfig();if(!phicommToggle.checked){musicSource='ma';localStorage.setItem('ken-music-source','ma');directSpeaker.close();directSpeakerStatus='offline'}};
  page.querySelector('#settings-ha-form').onsubmit=e=>{e.preventDefault();saveConnection(page.querySelector('#settings-ha-url').value,page.querySelector('#settings-ha-token').value,true,{},page.querySelector('#settings-speaker-address').value,page.querySelector('#settings-use-phicomm').checked)};
  $('content').replaceChildren(page);
}
function applyDisplaySettings(save=true){$('stage').style.filter=`brightness(${displaySettings.brightness/100})`;if(save)localStorage.setItem('ken-display-v3',JSON.stringify(displaySettings));resetIdle()}
function applyTheme(){
  const allowed=new Set(['default','spring','summer','autumn','winter','dark']),theme=allowed.has(config.theme)?config.theme:'default';
  document.documentElement.dataset.theme=theme;
  $('stage').dataset.theme=theme;
  $('theme-color').setAttribute('content',theme==='dark'?'#151b22':({spring:'#eef6e6',summer:'#fbf3e8',autumn:'#f4efe9',winter:'#edf4f8'}[theme]||'#f7f3eb'));
}
function resetIdle(){clearTimeout(idleTimer);$('screen-shade').hidden=true;if(displaySettings.timeout>0)idleTimer=setTimeout(()=>$('screen-shade').hidden=false,displaySettings.timeout*60000)}

async function restoreLightSnapshot(key,snapshot){
  if(!snapshot)return;
  if(activeLightPopup?.key===key)activeLightPopup.pendingBrightness=!snapshot.on?{raw:0,until:Date.now()+3000}:snapshot.brightness?{raw:snapshot.brightness,until:Date.now()+3000}:null;
  if(snapshot.on){
    const data={};
    if(snapshot.brightness)data.brightness=snapshot.brightness;
    if(snapshot.rgb)data.rgb_color=snapshot.rgb;
    else if(snapshot.kelvin)data.color_temp_kelvin=snapshot.kelvin;
    await call(key,'turn_on',data);
  }else{
    await call(key,'turn_off');
    if(snapshot.brightness)config.lastLightBrightness[config.entities[key]]=snapshot.brightness;
    if(snapshot.rgb)config.lastLightColors[key]=rgbToHex(snapshot.rgb);
    localStorage.setItem('ken-config-v3',JSON.stringify(config));
  }
}
function showCobDetail(key='cob',title='Đèn COB'){
  rememberLightStates();
  const pending=activeLightPopup?.key===key?activeLightPopup.pendingBrightness:null;
  const initialBrightness=lightColorData(key,{}).brightness??lightBrightness(key);
  resetLightPopup();
  const s=state(key),pct=lightDisplayPercent(key,initialBrightness);
  const modes=s?.attributes?.supported_color_modes||[];
  const detected=modes.some(mode=>['rgb','rgbw','rgbww','hs','xy'].includes(mode))?'rgb':modes.includes('color_temp')?'temperature':Array.isArray(s?.attributes?.rgb_color)?'rgb':s?.attributes?.color_temp_kelvin||s?.attributes?.color_temp?'temperature':null;
  let colorType=config.lightColorTypes?.[key];
  if(!colorType&&detected){colorType=detected;config.lightColorTypes={...(config.lightColorTypes||{}),[key]:detected};persistConfig()}
  const rgbMode=colorType==='rgb';
  const temperatureColors=[{k:2700,name:'Ấm',color:'#ffd39a'},{k:3000,name:'Vàng',color:'#ffe0ad'},{k:4000,name:'Trung tính',color:'#fff3d6'},{k:6500,name:'Trắng',color:'#e8f2ff'}];
  const rgbNames=['Vàng ấm','Xanh nhạt','Tím','Đỏ'],rgbDefaults=['#ffd08a','#9fd8ed','#b9a2df','#ed8d86'];
  const saved=config.rgbPresets?.[key]||[],normalized=saved.length>=5?saved.slice(1,5):saved.slice(0,4);
  const rgbColors=rgbDefaults.map((fallback,index)=>/^#[0-9a-f]{6}$/i.test(normalized[index]||'')?normalized[index]:fallback);
  $('detail-close').hidden=true;
  $('detail-content').innerHTML=`<div class="cob-popup-head"><span class="cob-popup-current" style="background:${lightDisplayColor(key)}"></span><h2>${escapeHtml(title)}</h2></div><div class="cob-brightness"><span>Độ sáng</span><b id="cob-brightness-value">${pct}%</b><input id="cob-brightness" type="range" min="0" max="100" value="${pct}" aria-label="Độ sáng"></div>${rgbMode?`<div class="cob-colors rgb-presets">${rgbColors.map((color,index)=>`<button type="button" aria-label="${rgbNames[index]}" data-rgb-index="${index}" data-color="${color}"><span style="background:${color}"></span></button>`).join('')}</div>`:`<div class="cob-colors temperature-presets">${temperatureColors.map(c=>`<button type="button" aria-label="${c.name} ${c.k}K" data-kelvin="${c.k}" data-color="${c.color}"><span style="background:${c.color}"></span></button>`).join('')}</div>`}<div class="color-dialog-actions"><button id="cob-done" type="button">Đóng</button></div>`;
  const slider=$('cob-brightness'),popup=bindPopupBrightness(key,slider,$('cob-brightness-value'));
  popup.pendingBrightness=pending;
  popup.currentColor=$('detail-content').querySelector('.cob-popup-current');
  const holdTimers=new Set();
  popup.cleanup=()=>{for(const timer of holdTimers)clearTimeout(timer)};
  $('detail-content').querySelectorAll('[data-kelvin]').forEach(button=>{
    button.onclick=()=>{button.blur();sendPopupColor(key,{color_temp_kelvin:Number(button.dataset.kelvin)})};
  });
  $('detail-content').querySelectorAll('[data-rgb-index]').forEach(button=>{
    let timer,held=false,startX=0,startY=0;
    const clearHold=()=>{clearTimeout(timer);holdTimers.delete(timer)};
    button.oncontextmenu=e=>e.preventDefault();
    button.onpointerdown=e=>{
      if(e.button>0)return;
      held=false;startX=e.clientX;startY=e.clientY;
      timer=setTimeout(()=>{holdTimers.delete(timer);held=true;markHeldPopupGesture(e.pointerId);showRgbPresetEditor(key,title,Number(button.dataset.rgbIndex),rgbColors)},600);
      holdTimers.add(timer);
    };
    button.onpointermove=e=>{if(Math.hypot(e.clientX-startX,e.clientY-startY)>10)clearHold()};
    for(const event of ['pointerup','pointercancel','pointerleave'])button.addEventListener(event,clearHold);
    button.onclick=()=>{
      button.blur();
      if(held){held=false;return}
      sendPopupColor(key,{rgb_color:hexToRgb(button.dataset.color)});
    };
  });
  $('cob-done').onclick=()=>$('detail-dialog').close();
  const dialog=$('detail-dialog');if(!dialog.open)dialog.showModal();
  syncLightPopup();focusDialogWithoutInput();
}
function hexToRgb(hex){return [1,3,5].map(index=>parseInt(hex.slice(index,index+2),16))}
function hexToHsv(hex){const [r,g,b]=hexToRgb(hex).map(value=>value/255),max=Math.max(r,g,b),min=Math.min(r,g,b),d=max-min;let h=0;if(d)h=max===r?60*((g-b)/d%6):max===g?60*((b-r)/d+2):60*((r-g)/d+4);return {h:(h+360)%360,s:max?d/max:0,v:max}}
function hsvToHex(h,s,v){const c=v*s,x=c*(1-Math.abs(h/60%2-1)),m=v-c;let rgb=h<60?[c,x,0]:h<120?[x,c,0]:h<180?[0,c,x]:h<240?[0,x,c]:h<300?[x,0,c]:[c,0,x];return '#'+rgb.map(n=>Math.round((n+m)*255).toString(16).padStart(2,'0')).join('')}
function showRgbPresetEditor(key,title,index,colors){
  const originalSnapshot=lightSnapshot(key);
  const pending=activeLightPopup?.key===key?activeLightPopup.pendingBrightness:null;
  resetLightPopup();
  $('detail-close').hidden=true;
  let {h,s}=hexToHsv(colors[index]),selected=colors[index],dragging=false,previewTimer=null,previewDirty=false;
  const brightness=lightDisplayPercent(key,originalSnapshot.brightness);
  $('detail-content').innerHTML=`<div class="rgb-editor"><h2>Chọn màu</h2><div id="rgb-color-palette" class="rgb-color-palette rgb-spectrum"><span id="rgb-palette-handle" class="rgb-palette-handle"></span></div><div class="rgb-editor-brightness"><span id="rgb-custom-preview" style="background:${selected}"></span><div><label for="rgb-editor-level">Độ sáng <b id="rgb-editor-level-value">${brightness}%</b></label><input id="rgb-editor-level" type="range" min="0" max="100" value="${brightness}"></div></div><div class="rgb-editor-actions"><button id="rgb-editor-cancel" type="button">Hủy</button><button id="rgb-editor-save" type="button">Lưu</button></div></div>`;
  const palette=$('rgb-color-palette'),handle=$('rgb-palette-handle'),preview=$('rgb-custom-preview');
  const popup=bindPopupBrightness(key,$('rgb-editor-level'),$('rgb-editor-level-value'),()=>({rgb_color:hexToRgb(selected)}));
  popup.pendingBrightness=pending;
  const stopPreview=()=>{clearTimeout(previewTimer);previewTimer=null};
  popup.cleanup=stopPreview;
  const flushPreview=()=>{
    stopPreview();
    if(!previewDirty||activeLightPopup!==popup)return;
    previewDirty=false;
    sendPopupColor(key,{rgb_color:hexToRgb(selected)});
  };
  const renderColor=()=>{
    const angle=h*Math.PI/180,radius=s*48;
    handle.style.left=`${50+Math.cos(angle)*radius}%`;handle.style.top=`${50+Math.sin(angle)*radius}%`;
    preview.style.background=selected;
  };
  const updatePointer=e=>{
    const rect=palette.getBoundingClientRect(),x=(e.clientX-rect.left)/rect.width-.5,y=(e.clientY-rect.top)/rect.height-.5;
    h=(Math.atan2(y,x)*180/Math.PI+360)%360;s=Math.min(1,Math.hypot(x,y)*2);
    selected=hsvToHex(h,s,1);renderColor();previewDirty=true;
    if(previewTimer==null)previewTimer=setTimeout(flushPreview,75);
  };
  palette.onpointerdown=e=>{if(e.button>0)return;e.preventDefault();dragging=true;palette.setPointerCapture?.(e.pointerId);updatePointer(e)};
  palette.onpointermove=e=>{if(dragging)updatePointer(e)};
  palette.onpointerup=e=>{if(!dragging)return;dragging=false;updatePointer(e);flushPreview()};
  palette.onpointercancel=()=>{dragging=false;flushPreview()};
  popup.onCancel=async()=>{
    stopPreview();previewDirty=false;
    $('rgb-editor-cancel').disabled=true;$('rgb-editor-save').disabled=true;
    await restoreLightSnapshot(key,originalSnapshot);
    if(activeLightPopup===popup)showCobDetail(key,title);
  };
  $('rgb-editor-cancel').onclick=popup.onCancel;
  $('rgb-editor-save').onclick=()=>{
    flushPreview();
    const updated=[...colors];updated[index]=selected;
    config.rgbPresets={...(config.rgbPresets||{}),[key]:updated};
    persistConfig();showCobDetail(key,title);toast('Đã lưu màu');
  };
  const dialog=$('detail-dialog');if(!dialog.open)dialog.showModal();
  renderColor();syncLightPopup();focusDialogWithoutInput();
}
function showDeviceDetail(def){
  const water=def.kind==='water',powerKey=def.powerEntity||(def.kind==='washer'?'washerPower':def.kind==='dryer'?'dryerPower':'inductionPower'),monthKey=def.monthEntity||(def.kind==='washer'?'washerMonthly':def.kind==='dryer'?'dryerMonthly':'inductionMonthly'),totalKey=def.totalEntity||(def.kind==='washer'?'washerTotal':def.kind==='dryer'?'dryerTotal':'inductionTotal'),rows=water?[['Hôm nay',formatSensor(def.dailyEntity||'waterToday',1)],['Tháng này',formatSensor(def.monthEntity||'waterMonth',1)],['Tổng',formatSensor(def.totalEntity||'waterTotal',1)]]:[['Công suất hiện tại',formatSensor(powerKey,0)],['Tháng này',formatSensor(monthKey,1)],['Tổng tiêu thụ',formatSensor(totalKey,1)]];
  $('detail-content').innerHTML=`<h2>${escapeHtml(def.name)}</h2><div class="detail-grid">${rows.map(([a,b])=>`<span>${a}</span><b>${escapeHtml(b)}</b>`).join('')}</div>`;$('detail-dialog').showModal();
}

function renderStatus(status){
  const text=({connecting:'ĐANG KẾT NỐI HA',ready:'ĐÃ KẾT NỐI HA',offline:'MẤT KẾT NỐI HA',auth_invalid:'TOKEN HA KHÔNG HỢP LỆ',demo:'BẢN MẪU'})[status]||(connected?'ĐÃ KẾT NỐI HA':'CHƯA KẾT NỐI HA');$('brand-status').textContent=text;
}

function openConfig(){
  $('ha-url').value=config.url;$('ha-token').value=savedToken();$('remember-token').checked=!!localStorage.getItem('ken-token');
  for(const [key] of Object.entries(DEFAULT_ENTITIES)){const input=$(`entity-${key}`);if(input)input.value=config.entities[key]||''}$('config-dialog').showModal();
}
function saveConnection(urlValue,tokenValue,remember,entityValues={},speakerAddress=config.speakerAddress,usePhicomm=config.usePhicomm){
  try{
    const url=urlValue.trim(),token=tokenValue.trim();if(url||token){if(!url||!token)throw Error('Cần nhập đủ địa chỉ HA và token');const parsed=new URL(url);if(!['http:','https:'].includes(parsed.protocol))throw Error('Địa chỉ HA phải bắt đầu bằng http:// hoặc https://')}
    const entities={...config.entities},fromDialog=$('config-dialog').open;for(const key of Object.keys(DEFAULT_ENTITIES)){const dialogInput=$(`entity-${key}`);const value=entityValues[key]??(fromDialog?dialogInput?.value:undefined);entities[key]=(value||entities[key]||DEFAULT_ENTITIES[key]).trim()}
    config={url,speakerAddress:String(speakerAddress||'').trim(),usePhicomm:Boolean(usePhicomm),theme:config.theme||'default',rgbPresets:config.rgbPresets||{},lightColorTypes:config.lightColorTypes||{},lastLightColors:config.lastLightColors||{},lastLightBrightness:config.lastLightBrightness||{},entities};persistConfig();if(token){const origin=urlOrigin(url);sessionStorage.setItem('ken-token',token);sessionStorage.setItem('ken-token-origin',origin);if(remember){localStorage.setItem('ken-token',token);localStorage.setItem('ken-token-origin',origin)}else{localStorage.removeItem('ken-token');localStorage.removeItem('ken-token-origin')}demo=false;connected=false;states={};client.connect(url,token)}if(!config.usePhicomm){musicSource='ma';localStorage.setItem('ken-music-source','ma');directSpeaker.close();directSpeakerStatus='offline'}else if(musicSource==='aibox'&&config.speakerAddress)directSpeaker.connect(config.speakerAddress);if($('config-dialog').open)$('config-dialog').close();render();toast('Đã lưu cấu hình Home Assistant và loa');
  }catch(error){toast(error.message)}
}
function forgetToken(){client.stop();for(const store of [sessionStorage,localStorage]){store.removeItem('ken-token');store.removeItem('ken-token-origin')}connected=false;demo=false;states={};renderStatus('disconnected');render();toast('Đã xóa token trên trình duyệt')}
function buildEntityForm(){
  const root=$('entity-form');root.replaceChildren();for(const [key,label] of entityCatalog()){const wrap=document.createElement('label');wrap.textContent=label;const input=document.createElement('input');input.id=`entity-${key}`;input.setAttribute('list','entity-list');input.autocomplete='off';wrap.append(input);root.append(wrap)}
}
function fillEntityList(){entityListDirty=false;const seen=new Set(),keyByEntity=new Map(),deviceByKey=new Map();for(const [key,entity] of Object.entries(config.entities))if(entity&&!keyByEntity.has(entity))keyByEntity.set(entity,key);for(const [key,def] of Object.entries(DEVICE))if(!deviceByKey.has(def.entity))deviceByKey.set(def.entity,key);$('entity-list').replaceChildren(...Object.values(states).filter(s=>{if(seen.has(s.entity_id))return false;seen.add(s.entity_id);return true}).map(s=>{const o=document.createElement('option'),entityKey=keyByEntity.get(s.entity_id),deviceKey=entityKey&&deviceByKey.get(entityKey);o.value=s.entity_id;o.label=layoutEdits.names[entityKey]||layoutEdits.names[deviceKey]||s.attributes?.friendly_name||s.entity_id;return o}))}
async function refreshEntityStates(){try{if(client.ready){const fresh=await client.request({type:'get_states'});client.states=Object.fromEntries(fresh.map(s=>[s.entity_id,s]));states=client.states;rememberLightStates();syncLightPopup()}fillEntityList();toast(`Đã làm mới ${Object.keys(states).length} entity từ Home Assistant`)}catch(error){toast(error.message)}}

function loadDemo(){
  client.stop();demo=true;connected=false;states={};const put=(key,value,attributes={})=>states[config.entities[key]]={entity_id:config.entities[key],state:String(value),attributes:{friendly_name:ENTITY_LABELS[key],...attributes}};
  put('weather','partlycloudy',{temperature:30,humidity:72});put('temperature',29.4,{unit_of_measurement:'°C'});put('humidity',71,{unit_of_measurement:'%'});put('pm25',12,{unit_of_measurement:'µg/m³'});put('air','Tốt');put('power',1240,{unit_of_measurement:'W'});put('energy',286.4,{unit_of_measurement:'kWh'});put('cost',824000,{unit_of_measurement:'VND'});put('bedroomTemperature',27.2,{unit_of_measurement:'°C'});put('bedroomHumidity',68,{unit_of_measurement:'%'});put('bedroomCo2',612,{unit_of_measurement:'ppm'});
  ['eating','sofa','ceiling','balcony','bedroomLight','kitchenLight','wcLight'].forEach((k,i)=>put(k,i<2?'on':'off'));['lobby','hallway','bedroomBathLight','waterHeater','hood','ceilingFan','bedroomFan'].forEach(k=>put(k,'off'));put('cob','on',{brightness:178,color_temp_kelvin:4000,supported_color_modes:['brightness','color_temp']});put('ac','cool',{temperature:26,min_temp:16,max_temp:30,target_temp_step:1,hvac_modes:['off','cool','dry','fan_only']});put('toshiba','on',{percentage:66,oscillating:true,supported_features:3});put('cover','open',{current_position:65,supported_features:15});put('rain','off');put('door','off');put('window','off');put('balconyDoor','off');put('washerActive','off');put('washerFinish','on');put('washerRemaining','0:00',{unit_of_measurement:'min'});put('dryer','off');put('washerDaily',220,{unit_of_measurement:'Wh'});put('dryerDaily',840,{unit_of_measurement:'Wh'});put('washerPower',0,{unit_of_measurement:'W'});put('dryerPower',0,{unit_of_measurement:'W'});put('waterFlow',0,{unit_of_measurement:'L/min'});put('waterToday',82.4,{unit_of_measurement:'L'});put('waterMonth',1240,{unit_of_measurement:'L'});put('waterTotal',18420,{unit_of_measurement:'L'});put('inductionStatus','off');put('inductionPower',0,{unit_of_measurement:'W'});put('inductionDaily',0.8,{unit_of_measurement:'kWh'});put('inductionMonthly',18.3,{unit_of_measurement:'kWh'});put('inductionTotal',620,{unit_of_measurement:'kWh'});put('piTemp',48.2,{unit_of_measurement:'°C'});put('piDisk',78.5,{unit_of_measurement:'GB'});put('piRam',31,{unit_of_measurement:'%'});put('music','paused',{media_title:'Một bài hát cho KEN',media_artist:'Music Assistant',media_album_name:'Nhạc trong nhà',volume_level:.32,is_volume_muted:false,source_list:['Music Assistant','AirPlay','AI BOX']});renderStatus('demo');render();$('config-dialog').close();
}

function tick(){syncLightPopup();const now=new Date();$('clock').textContent=now.toLocaleTimeString('vi-VN',{hour:'2-digit',minute:'2-digit',hour12:false});$('date').textContent=now.toLocaleDateString('vi-VN',{day:'2-digit',month:'2-digit',year:'numeric'});document.querySelectorAll('.washer-remaining-value').forEach(node=>node.textContent=formatRemaining(node.dataset.remainingKey));const seek=$('music-seek');if(seek&&!seek.matches(':active')){let position;if(musicSource==='aibox'){if(directSpeakerState.playing&&directSpeakerState.duration)directSpeakerState.position=Math.min(directSpeakerState.duration,(directSpeakerState.position||0)+1);position=directSpeakerState.position}else{const s=state('music'),a=s?.attributes||{};position=currentMediaPosition(s,a,s?.state==='playing',Number(a.media_duration)||0)}seek.value=String(position||0);const label=$('seek-current');if(label)label.textContent=formatPlaybackTime(position)}}
function resizeStage(){const scale=Math.min(innerWidth/1024,innerHeight/600);$('stage').style.transform=`translate(-50%,-50%) scale(${scale})`}

async function startDashboard(){
  await loadSharedDashboard();applyTheme();buildNavigation();buildEntityForm();installTopEntityEditors();installGestureGuards();tick();setInterval(tick,1000);setInterval(pollSharedDashboard,5000);resizeStage();addEventListener('resize',resizeStage);applyDisplaySettings(false);
  $('config-close').onclick=()=>$('config-dialog').close();$('detail-close').onclick=()=>$('detail-dialog').close();$('screen-shade').onclick=resetIdle;
  $('brand-card').onclick=openConfig;$('brand-card').onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();openConfig()}};
  $('detail-dialog').addEventListener('close',()=>{metricEditorRoomId=null;resetLightPopup();$('detail-close').hidden=false});
  $('detail-dialog').addEventListener('cancel',event=>{if(activeLightPopup?.onCancel){event.preventDefault();activeLightPopup.onCancel()}});
  for(const name of ['pointerdown','keydown','touchstart'])addEventListener(name,resetIdle,{passive:true});
  document.addEventListener('focusin',e=>{if(entityListDirty&&e.target.matches?.('input[list="entity-list"]'))fillEntityList()});
  for(const name of ['pointerup','pointercancel'])addEventListener(name,()=>setTimeout(()=>{rangeDragging=false},800),{passive:true});
  $('demo-button').onclick=loadDemo;$('forget-button').onclick=forgetToken;
  $('config-form').onsubmit=e=>{e.preventDefault();saveConnection($('ha-url').value,$('ha-token').value,$('remember-token').checked)};
  const token=savedToken();if(config.url&&token){try{client.connect(config.url,token)}catch(error){toast(error.message);loadDemo()}}else{loadDemo();if(config.url&&hasStoredToken())toast('Địa chỉ Home Assistant đã đổi. Vào Cài đặt nhập lại token để kết nối.')}
  document.addEventListener('visibilitychange',()=>{if(!document.hidden&&!demo&&config.url){const token=savedToken();if(token)try{client.connect(config.url,token)}catch(error){toast(error.message)}}pollSharedDashboard()});
}
startDashboard();
