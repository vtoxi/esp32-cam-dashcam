import { Component, computed, inject, signal } from '@angular/core';
import { RouterLink, RouterLinkActive, RouterOutlet } from '@angular/router';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { AdminAuthService } from '../../core/services/admin-auth.service';
import { ApiConfigService } from '../../core/services/api-config.service';
import { EventStreamService } from '../../core/services/event-stream.service';

interface NavItem {
  path: string;
  label: string;
  icon: string;
}

const NAV_ITEMS: NavItem[] = [
  { path: '/dashboard', label: 'Dashboard', icon: 'squares-2x2' },
  { path: '/devices', label: 'Devices', icon: 'device-phone' },
  { path: '/telemetry', label: 'Telemetry', icon: 'signal' },
  { path: '/events', label: 'Events', icon: 'bell-alert' },
  { path: '/incidents', label: 'Incidents', icon: 'exclamation-triangle' },
  { path: '/commands', label: 'Commands', icon: 'terminal' },
  { path: '/webhooks', label: 'Webhooks', icon: 'link' },
  { path: '/activity', label: 'Live Activity', icon: 'bolt' },
];

/** App shell: fixed sidenav (collapsible on mobile) + topbar with live
 * connection status, mounted once around every routed page. */
@Component({
  selector: 'app-shell',
  standalone: true,
  imports: [RouterOutlet, RouterLink, RouterLinkActive, IconComponent],
  templateUrl: './shell.component.html',
})
export class ShellComponent {
  readonly stream = inject(EventStreamService);
  readonly admin = inject(AdminAuthService);
  readonly apiConfig = inject(ApiConfigService);

  readonly navItems = NAV_ITEMS;
  readonly mobileNavOpen = signal(false);

  readonly connectionLabel = computed(() => (this.stream.connected() ? 'Live' : 'Reconnecting…'));
}
