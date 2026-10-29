import { Component, Input, OnInit, inject, signal } from '@angular/core';
import { Router } from '@angular/router';
import { DataTableColumn, DataTableComponent } from '../../shared/components/data-table/data-table.component';
import { CellTemplateDirective } from '../../shared/components/data-table/cell-template.directive';
import { PageHeaderComponent } from '../../shared/components/page-header/page-header.component';
import { StatusBadgeComponent } from '../../shared/components/status-badge/status-badge.component';
import { JsonViewerComponent } from '../../shared/components/json-viewer/json-viewer.component';
import { IconComponent } from '../../shared/components/icon/icon.component';
import { RelativeTimePipe } from '../../shared/pipes/relative-time.pipe';
import { CommandRecord, Device, EventRecord, IncidentRecord, TelemetryRecord } from '../../core/models';
import { DeviceService } from '../../core/services/device.service';
import { EventLogService } from '../../core/services/telemetry.service';
import { IncidentService } from '../../core/services/incident.service';
import { CommandService } from '../../core/services/command.service';
import { IssueCommandDialogComponent } from '../commands/issue-command-dialog.component';
import { EditDeviceDialogComponent } from './edit-device-dialog.component';
import { DialogService } from '../../shared/components/dialog/dialog.service';
import { NotificationService } from '../../core/services/notification.service';
import { AdminAuthService } from '../../core/services/admin-auth.service';

type TabId = 'overview' | 'telemetry' | 'events' | 'incidents' | 'commands';

@Component({
  selector: 'app-device-detail',
  standalone: true,
  imports: [
    DataTableComponent,
    CellTemplateDirective,
    PageHeaderComponent,
    StatusBadgeComponent,
    JsonViewerComponent,
    IconComponent,
    RelativeTimePipe,
  ],
  templateUrl: './device-detail.component.html',
})
export class DeviceDetailComponent implements OnInit {
  @Input() id = '';

  private readonly deviceService = inject(DeviceService);
  private readonly eventService = inject(EventLogService);
  private readonly incidentService = inject(IncidentService);
  private readonly commandService = inject(CommandService);
  private readonly dialogs = inject(DialogService);
  private readonly notifications = inject(NotificationService);
  private readonly router = inject(Router);
  readonly admin = inject(AdminAuthService);

  readonly device = signal<Device | null>(null);
  readonly loadingDevice = signal(true);
  readonly notFound = signal(false);

  readonly activeTab = signal<TabId>('overview');
  readonly loadedTabs = new Set<TabId>();

  readonly telemetry = signal<TelemetryRecord[]>([]);
  readonly telemetryLoading = signal(false);
  readonly events = signal<EventRecord[]>([]);
  readonly eventsLoading = signal(false);
  readonly incidents = signal<IncidentRecord[]>([]);
  readonly incidentsLoading = signal(false);
  readonly commands = signal<CommandRecord[]>([]);
  readonly commandsLoading = signal(false);

  readonly telemetryColumns: DataTableColumn<TelemetryRecord>[] = [
    { key: 'receivedAt', header: 'Received' },
    { key: 'payloadJson', header: 'Payload', sortable: false },
  ];
  readonly eventColumns: DataTableColumn<EventRecord>[] = [
    { key: 'receivedAt', header: 'Received' },
    { key: 'payloadJson', header: 'Payload', sortable: false },
  ];
  readonly incidentColumns: DataTableColumn<IncidentRecord>[] = [
    { key: 'incidentId', header: 'Incident ID' },
    { key: 'state', header: 'State' },
    { key: 'firstReceivedAt', header: 'First Seen' },
    { key: 'lastReceivedAt', header: 'Last Update' },
  ];
  readonly commandColumns: DataTableColumn<CommandRecord>[] = [
    { key: 'commandType', header: 'Type' },
    { key: 'status', header: 'Status' },
    { key: 'issuedAt', header: 'Issued' },
    { key: 'completedAt', header: 'Completed', value: (r) => r.completedAt ?? '' },
  ];

  ngOnInit(): void {
    this.loadDevice();
  }

  loadDevice(): void {
    this.loadingDevice.set(true);
    this.deviceService.get(this.id).subscribe({
      next: (device) => {
        this.device.set(device);
        this.loadingDevice.set(false);
      },
      error: () => {
        this.notFound.set(true);
        this.loadingDevice.set(false);
      },
    });
  }

  selectTab(tab: TabId): void {
    this.activeTab.set(tab);
    if (this.loadedTabs.has(tab)) return;
    this.loadedTabs.add(tab);
    switch (tab) {
      case 'telemetry':
        this.telemetryLoading.set(true);
        this.deviceService.telemetry(this.id, 200).subscribe((rows) => {
          this.telemetry.set(rows);
          this.telemetryLoading.set(false);
        });
        break;
      case 'events':
        this.eventsLoading.set(true);
        this.eventService.list(this.id, 200).subscribe((rows) => {
          this.events.set(rows);
          this.eventsLoading.set(false);
        });
        break;
      case 'incidents':
        this.incidentsLoading.set(true);
        this.incidentService.list(this.id, 200).subscribe((rows) => {
          this.incidents.set(rows);
          this.incidentsLoading.set(false);
        });
        break;
      case 'commands':
        this.loadCommands();
        break;
    }
  }

  loadCommands(): void {
    this.commandsLoading.set(true);
    this.commandService.history(this.id, 100).subscribe((rows) => {
      this.commands.set(rows);
      this.commandsLoading.set(false);
    });
  }

  async issueCommand(): Promise<void> {
    const result = await this.dialogs.open(IssueCommandDialogComponent, { deviceId: this.id });
    if (result) {
      this.notifications.success('Command issued.');
      this.loadCommands();
    }
  }

  async editDevice(): Promise<void> {
    const d = this.device();
    if (!d) return;
    const result = await this.dialogs.open(EditDeviceDialogComponent, { device: d });
    if (result) {
      this.notifications.success('Device updated.');
      this.loadDevice();
    }
  }

  async deleteDevice(): Promise<void> {
    const d = this.device();
    if (!d) return;
    const confirmed = await this.dialogs.confirm({
      title: 'Delete device',
      message: `Delete ${d.id}? Historical telemetry, events, and incidents for this device are kept; only the device record itself is removed.`,
      confirmLabel: 'Delete',
      danger: true,
    });
    if (!confirmed) return;
    this.deviceService.delete(d.id).subscribe(() => {
      this.notifications.success('Device deleted.');
      this.router.navigate(['/devices']);
    });
  }

  openIncident(incidentId: string): void {
    this.router.navigate(['/incidents', incidentId]);
  }

  readonly telemetryRowKey = (r: TelemetryRecord) => r.id;
  readonly eventRowKey = (r: EventRecord) => r.id;
  readonly incidentRowKey = (r: IncidentRecord) => r.incidentId;
  readonly commandRowKey = (r: CommandRecord) => r.commandId;
}
