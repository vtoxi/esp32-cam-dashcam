import { Routes } from '@angular/router';

export const routes: Routes = [
  {
    path: '',
    loadComponent: () => import('./layout/shell/shell.component').then((m) => m.ShellComponent),
    children: [
      { path: '', pathMatch: 'full', redirectTo: 'dashboard' },
      {
        path: 'dashboard',
        loadComponent: () => import('./features/dashboard/dashboard.component').then((m) => m.DashboardComponent),
        title: 'Dashboard · CarSentinel',
      },
      {
        path: 'devices',
        loadComponent: () => import('./features/devices/device-list.component').then((m) => m.DeviceListComponent),
        title: 'Devices · CarSentinel',
      },
      {
        path: 'devices/:id',
        loadComponent: () =>
          import('./features/devices/device-detail.component').then((m) => m.DeviceDetailComponent),
        title: 'Device · CarSentinel',
      },
      {
        path: 'telemetry',
        loadComponent: () =>
          import('./features/telemetry/telemetry-list.component').then((m) => m.TelemetryListComponent),
        title: 'Telemetry · CarSentinel',
      },
      {
        path: 'events',
        loadComponent: () => import('./features/events/event-list.component').then((m) => m.EventListComponent),
        title: 'Events · CarSentinel',
      },
      {
        path: 'incidents',
        loadComponent: () =>
          import('./features/incidents/incident-list.component').then((m) => m.IncidentListComponent),
        title: 'Incidents · CarSentinel',
      },
      {
        path: 'incidents/:id',
        loadComponent: () =>
          import('./features/incidents/incident-detail.component').then((m) => m.IncidentDetailComponent),
        title: 'Incident · CarSentinel',
      },
      {
        path: 'commands',
        loadComponent: () =>
          import('./features/commands/command-list.component').then((m) => m.CommandListComponent),
        title: 'Commands · CarSentinel',
      },
      {
        path: 'webhooks',
        loadComponent: () =>
          import('./features/webhooks/webhook-list.component').then((m) => m.WebhookListComponent),
        title: 'Webhooks · CarSentinel',
      },
      {
        path: 'activity',
        loadComponent: () =>
          import('./features/activity/live-activity.component').then((m) => m.LiveActivityComponent),
        title: 'Live Activity · CarSentinel',
      },
      {
        path: 'settings',
        loadComponent: () => import('./features/settings/settings.component').then((m) => m.SettingsComponent),
        title: 'Settings · CarSentinel',
      },
    ],
  },
  { path: '**', redirectTo: 'dashboard' },
];
