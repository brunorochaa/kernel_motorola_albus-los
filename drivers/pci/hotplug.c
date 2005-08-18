#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/module.h>
#include "pci.h"

int pci_hotplug (struct device *dev, char **envp, int num_envp,
		 char *buffer, int buffer_size)
{
	struct pci_dev *pdev;
	char *scratch;
	int i = 0;
	int length = 0;

	if (!dev)
		return -ENODEV;

	pdev = to_pci_dev(dev);
	if (!pdev)
		return -ENODEV;

	scratch = buffer;

	/* stuff we want to pass to /sbin/hotplug */
	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "PCI_CLASS=%04X",
			    pdev->class);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "PCI_ID=%04X:%04X",
			    pdev->vendor, pdev->device);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length,
			    "PCI_SUBSYS_ID=%04X:%04X", pdev->subsystem_vendor,
			    pdev->subsystem_device);
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;
	++length;
	scratch += length;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length, "PCI_SLOT_NAME=%s",
			    pci_name(pdev));
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;

	envp[i++] = scratch;
	length += scnprintf (scratch, buffer_size - length,
			    "MODALIAS=pci:v%08Xd%08Xsv%08Xsd%08Xbc%02Xsc%02Xi%02x",
			    pdev->vendor, pdev->device,
			    pdev->subsystem_vendor, pdev->subsystem_device,
			    (u8)(pdev->class >> 16), (u8)(pdev->class >> 8),
			    (u8)(pdev->class));
	if ((buffer_size - length <= 0) || (i >= num_envp))
		return -ENOMEM;

	envp[i] = NULL;

	return 0;
}
